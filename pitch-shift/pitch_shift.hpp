#pragma once

// Pitch Shift — a real-time phase-vocoder pitch shifter.
//
// Clean-room textbook phase vocoder (Reiss & McPherson, "Audio Effects",
// chapter on the phase vocoder; cross-checked against the JUCE "Pitch Shift"
// example and the truce Rust port for the parameter set only — the DSP below was
// written from the book's STFT/phase-vocoder recipe, no effect source copied):
//
//   1. STFT analysis  — slide an FFT-sized window over the input ring every
//      `hop` samples, apply sqrt(window), forward-FFT.
//   2. Phase-vocoder frequency estimation — for each bin, measure the true
//      instantaneous frequency from the phase advance between hops
//      (princ-arg of the heterodyned deviation), then re-synthesise a phase
//      that advances at `ratio` times that rate.
//   3. Resynthesis — inverse-FFT, resample the frame by 1/ratio (linear
//      interp) so the pitch moves while duration is preserved, apply
//      sqrt(window), and overlap-add into the output ring.
//
// FFT size, hop (overlap) and window type are live parameters, exposed as combo
// boxes. Switching them rebuilds the FFT plan / buffers on the next block; that
// allocates on the audio thread, which is acceptable for a teaching example
// (the production Pulp engine, pulp::signal::RealtimePitchTimeProcessor, is the
// allocation-free path for shipping work).
//
// The FFT is Pulp's own pulp::signal::Fft (radix-2, vDSP-accelerated on Apple).

#include <pulp/format/processor.hpp>
// Headless WASM DSP builds curate out core/view (canvas/Skia/text-shaping),
// so every editor reference below is gated on PULP_HEADLESS.
#if !PULP_HEADLESS
#include <pulp/view/view.hpp>
#endif
#include <pulp/signal/fft.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

namespace pulp::examples::classic {

enum PitchShiftParams : state::ParamID {
    kShift   = 1,  // -12..12 semitones
    kPsFftSize = 2,  // combo index 0..4 -> 256 / 512 / 1024 / 2048 / 4096
    kPsHop     = 3,  // combo index 0..2 -> 1/2, 1/4, 1/8 overlap (2, 4, 8)
    kPsWindow  = 4,  // combo index 0..2 -> Bartlett, Hann, Hamming
};

// Option tables shared by the processor (decode combo indices) and the editor
// (combo labels). Order is the canonical truce / book order.
inline constexpr std::array<int, 5> kPitchFftSizes{256, 512, 1024, 2048, 4096};
inline constexpr std::array<int, 3> kPitchOverlaps{2, 4, 8};  // 1/2, 1/4, 1/8
enum class PitchWindow : int { Bartlett = 0, Hann = 1, Hamming = 2 };

inline constexpr float kPitchTwoPi = 6.28318530717958647692f;
inline constexpr float kPitchPi    = 3.14159265358979323846f;

// Build an `len`-sample window of the requested type into `out` (resized).
inline void build_pitch_window(int type, int len, std::vector<float>& out) {
    out.assign(static_cast<std::size_t>(std::max(1, len)), 0.0f);
    if (len <= 1) { out[0] = 1.0f; return; }
    const float denom = static_cast<float>(len - 1);
    for (int i = 0; i < len; ++i) {
        const float n = static_cast<float>(i);
        switch (static_cast<PitchWindow>(type)) {
            case PitchWindow::Bartlett:
                out[i] = 1.0f - std::fabs(2.0f * n / denom - 1.0f);
                break;
            case PitchWindow::Hamming:
                out[i] = 0.54f - 0.46f * std::cos(kPitchTwoPi * n / denom);
                break;
            case PitchWindow::Hann:
            default:
                out[i] = 0.5f * (1.0f - std::cos(kPitchTwoPi * n / denom));
                break;
        }
    }
}

// Overlap-add normalisation: N / overlap / sum(window). Together with the
// sqrt(window) applied on both analysis and synthesis this reconstructs unity
// gain for a COLA window at ratio 1.
inline float pitch_window_scale(const std::vector<float>& window, int overlap) {
    float sum = 0.0f;
    for (float v : window) sum += v;
    if (overlap <= 0 || sum == 0.0f) return 0.0f;
    return static_cast<float>(window.size()) / static_cast<float>(overlap) / sum;
}

// Wrap a phase into [-pi, pi]. The accumulators need this every hop or rounding
// error spirals away.
inline float pitch_princ_arg(float phase) {
    return phase - kPitchTwoPi * std::round(phase / kPitchTwoPi);
}

// Defined out-of-line in pitch_shift_editor.hpp (included at the bottom of this
// file). Forward-declared so create_view() hands the host the same dark Ink &
// Signal editor the screenshot tests render.
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
std::unique_ptr<view::View> build_pitch_shift_editor(state::StateStore& store);
#endif

class PitchShiftProcessor : public format::Processor {
public:
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
    std::unique_ptr<view::View> create_view() override { return build_pitch_shift_editor(state()); }
#endif

    format::PluginDescriptor descriptor() const override {
        return {.name = "Pitch Shift", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.pitch-shift", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        store.add_parameter({.id = kShift, .name = "Shift", .unit = "st",
                             .range = {-12.0f, 12.0f, 0.0f, 0.0f}});
        // Discrete combo params: {min, max, default-index, step=1}.
        store.add_parameter({.id = kPsFftSize, .name = "FFT Size", .unit = "",
                             .range = {0.0f, 4.0f, 1.0f, 1.0f}});   // default 512
        store.add_parameter({.id = kPsHop, .name = "Hop", .unit = "",
                             .range = {0.0f, 2.0f, 2.0f, 1.0f}});   // default 1/8
        store.add_parameter({.id = kPsWindow, .name = "Window", .unit = "",
                             .range = {0.0f, 2.0f, 1.0f, 1.0f}});   // default Hann
    }

    // An STFT pitch shifter buffers a full analysis window before it can emit
    // the first synthesised hop, so the algorithmic latency is one FFT frame.
    int latency_samples() const override { return pv_.fft_size > 0 ? pv_.fft_size : 512; }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        // Seed the vocoder at the default config so latency_samples() is sane
        // before the first process() call.
        pv_.configure(kPitchFftSizes[1], kPitchOverlaps[2],
                      static_cast<int>(PitchWindow::Hann), 2);
        pv_.reset();
    }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const int channels = static_cast<int>(
            std::min(output.num_channels(), input.num_channels()));
        const int frames = static_cast<int>(output.num_samples());

        const int fft_size = kPitchFftSizes[combo_index(kPsFftSize, kPitchFftSizes.size())];
        const int overlap  = kPitchOverlaps[combo_index(kPsHop, kPitchOverlaps.size())];
        const int window   = combo_index(kPsWindow, 3);
        pv_.configure(fft_size, overlap, window, std::max(1, channels));

        if (ctx.should_reset_dsp_state()) pv_.reset();

        const int hop = std::max(1, pv_.hop_size);
        const float hop_f = static_cast<float>(hop);

        // Snap the pitch ratio so the per-hop phase advance is an integer
        // multiple of the bin spacing (the book's recipe): eliminates the slow
        // phase drift that otherwise builds up between successive hops.
        const float shift = state().get_value(kShift);
        const float raw   = std::pow(2.0f, shift / 12.0f);
        float ratio = std::round(raw * hop_f) / hop_f;
        ratio = std::max(ratio, 1e-3f);

        int resampled_len = static_cast<int>(std::floor(pv_.fft_size / ratio));
        resampled_len = std::clamp(resampled_len, 1, pv_.output_len);

        // Rebuild the synthesis window each block so its length tracks the
        // resampled frame; cheap relative to the FFTs.
        build_pitch_window(pv_.window_type, resampled_len, pv_.synthesis_window);

        // Snapshot the shared ring positions so every channel runs the identical
        // hop schedule (each channel keeps its own phase accumulators).
        const int saved_in    = pv_.input_write_pos;
        const int saved_out_r = pv_.output_read_pos;
        const int saved_out_w = pv_.output_write_pos;
        const int saved_count = pv_.samples_since_last_fft;
        const bool saved_reset = pv_.need_phase_reset;

        for (int ch = 0; ch < channels; ++ch) {
            pv_.input_write_pos       = saved_in;
            pv_.output_read_pos       = saved_out_r;
            pv_.output_write_pos      = saved_out_w;
            pv_.samples_since_last_fft = saved_count;
            pv_.need_phase_reset      = saved_reset;

            const float* in = input.channel(ch).data();
            float* out = output.channel(ch).data();
            auto& in_ring  = pv_.input_buffer[ch];
            auto& out_ring = pv_.output_buffer[ch];

            for (int i = 0; i < frames; ++i) {
                // Capture the input sample BEFORE writing the output. Most AU /
                // VST / CLAP hosts render in place — `out` aliases `in` — so
                // writing out[i] first would clobber in[i] before it reaches the
                // analysis ring. During priming the output ring is all zeros, so
                // the clobber fed the vocoder silence and the plugin was silent
                // in every in-place host (fine in the separate-buffer tests).
                const float x = in[i];

                // Read the synthesised output (ring is pre-filled with zeros),
                // then clear that slot for the next overlap-add pass.
                const float y = out_ring[pv_.output_read_pos];
                out_ring[pv_.output_read_pos] = 0.0f;
                if (++pv_.output_read_pos >= pv_.output_len) pv_.output_read_pos = 0;
                out[i] = y;

                in_ring[pv_.input_write_pos] = x;
                if (++pv_.input_write_pos >= pv_.fft_size) pv_.input_write_pos = 0;

                if (++pv_.samples_since_last_fft >= hop) {
                    pv_.samples_since_last_fft = 0;
                    do_hop(ch, ratio, resampled_len);
                    pv_.output_write_pos += hop;
                    while (pv_.output_write_pos >= pv_.output_len)
                        pv_.output_write_pos -= pv_.output_len;
                }
            }
        }

        // Zero any output channels the input didn't cover.
        for (std::size_t ch = static_cast<std::size_t>(channels);
             ch < output.num_channels(); ++ch) {
            auto o = output.channel(ch);
            for (std::size_t i = 0; i < output.num_samples(); ++i) o[i] = 0.0f;
        }
    }

private:
    // One analysis/synthesis hop for a single channel.
    void do_hop(int ch, float ratio, int resampled_len) {
        const int N = pv_.fft_size;
        const int bins = pv_.num_bins;
        const float hop_f = static_cast<float>(pv_.hop_size);
        auto& spec = pv_.spectrum;

        // Analysis: pull N samples out of the input ring (oldest first), apply
        // sqrt(window). sqrt because synthesis applies the matching sqrt(window)
        // — together they reproduce the unit-overlap window energy.
        int idx = pv_.input_write_pos;
        const auto& in_ring = pv_.input_buffer[ch];
        for (int i = 0; i < N; ++i) {
            const float w = std::sqrt(std::max(0.0f, pv_.window[i]));
            spec[i] = std::complex<float>(w * in_ring[idx], 0.0f);
            if (++idx >= N) idx = 0;
        }

        pv_.fft->forward(spec.data());

        if (pv_.need_phase_reset) {
            std::fill(pv_.input_phase[ch].begin(), pv_.input_phase[ch].end(), 0.0f);
            std::fill(pv_.output_phase[ch].begin(), pv_.output_phase[ch].end(), 0.0f);
            pv_.need_phase_reset = false;
        }

        auto& in_phase  = pv_.input_phase[ch];
        auto& out_phase = pv_.output_phase[ch];
        for (int k = 0; k < bins; ++k) {
            const float re = spec[k].real();
            const float im = spec[k].imag();
            const float mag = std::sqrt(re * re + im * im);
            const float phase = std::atan2(im, re);

            const float phase_dev = phase - in_phase[k] - pv_.omega[k] * hop_f;
            const float delta_phi = pv_.omega[k] * hop_f + pitch_princ_arg(phase_dev);
            const float new_phase = pitch_princ_arg(out_phase[k] + delta_phi * ratio);

            in_phase[k]  = phase;
            out_phase[k] = new_phase;
            spec[k] = std::complex<float>(mag * std::cos(new_phase), mag * std::sin(new_phase));
        }

        // DC and Nyquist carry no phase — keep them real so the IFFT output is
        // real, then mirror the conjugate-symmetric upper half for the
        // full-complex inverse transform.
        spec[0]     = std::complex<float>(spec[0].real(), 0.0f);
        spec[N / 2] = std::complex<float>(spec[N / 2].real(), 0.0f);
        for (int k = 1; k < N / 2; ++k) spec[N - k] = std::conj(spec[k]);

        pv_.fft->inverse(spec.data());   // normalised by 1/N

        // Resample by 1/ratio (linear interp) — this is what actually moves the
        // pitch — and apply sqrt(synthesis window).
        const float n_f  = static_cast<float>(N);
        const float rl_f = static_cast<float>(resampled_len);
        for (int j = 0; j < resampled_len; ++j) {
            const float x = static_cast<float>(j) * n_f / rl_f;
            const float fx = std::floor(x);
            const int i_low = static_cast<int>(fx) % N;
            const float dx = x - fx;
            const float s0 = spec[i_low].real();
            const float s1 = spec[(i_low + 1) % N].real();
            const float sw = std::sqrt(std::max(0.0f, pv_.synthesis_window[j]));
            pv_.resampled[j] = (s0 + dx * (s1 - s0)) * sw;
        }

        // Overlap-add into the output ring at output_write_pos.
        int oidx = pv_.output_write_pos;
        auto& out_ring = pv_.output_buffer[ch];
        for (int j = 0; j < resampled_len; ++j) {
            out_ring[oidx] += pv_.resampled[j] * pv_.window_scale;
            if (++oidx >= pv_.output_len) oidx = 0;
        }
    }

    int combo_index(state::ParamID id, std::size_t count) const {
        const int v = static_cast<int>(std::lround(state().get_value(id)));
        return std::clamp(v, 0, static_cast<int>(count) - 1);
    }

    // Phase-vocoder state. Buffers are rebuilt when FFT size / channel count
    // changes; the window and hop are rebuilt when overlap / window changes.
    struct PhaseVocoder {
        int fft_size = 0, overlap = 0, hop_size = 0, window_type = -1;
        int num_bins = 0, output_len = 0, channels = 0;

        std::unique_ptr<signal::Fft> fft;
        std::vector<float> window;            // analysis window (sqrt applied in loop)
        float window_scale = 0.0f;
        std::vector<float> omega;             // expected per-hop phase advance / hop
        std::vector<std::complex<float>> spectrum;  // full-size FFT scratch
        std::vector<float> synthesis_window;  // rebuilt per block, length resampled_len
        std::vector<float> resampled;         // resynthesis scratch

        std::vector<std::vector<float>> input_buffer;
        std::vector<std::vector<float>> output_buffer;
        std::vector<std::vector<float>> input_phase;
        std::vector<std::vector<float>> output_phase;

        int input_write_pos = 0, output_write_pos = 0, output_read_pos = 0;
        int samples_since_last_fft = 0;
        bool need_phase_reset = true;

        void configure(int new_fft, int new_overlap, int new_window, int new_channels) {
            const bool size_changed = new_fft != fft_size || new_channels != channels;
            if (size_changed) {
                fft_size = new_fft;
                channels = new_channels;
                num_bins = fft_size / 2 + 1;
                output_len = fft_size * 2;   // worst-case: ratio 0.5 doubles the frame
                fft = std::make_unique<signal::Fft>(fft_size);
                spectrum.assign(static_cast<std::size_t>(fft_size), {});
                resampled.assign(static_cast<std::size_t>(output_len), 0.0f);
                synthesis_window.assign(static_cast<std::size_t>(output_len), 0.0f);
                omega.assign(static_cast<std::size_t>(num_bins), 0.0f);
                for (int k = 0; k < num_bins; ++k)
                    omega[k] = kPitchTwoPi * static_cast<float>(k) / static_cast<float>(fft_size);
                const auto fcap = static_cast<std::size_t>(fft_size);
                const auto ocap = static_cast<std::size_t>(output_len);
                const auto bcap = static_cast<std::size_t>(num_bins);
                input_buffer.assign(channels, std::vector<float>(fcap, 0.0f));
                output_buffer.assign(channels, std::vector<float>(ocap, 0.0f));
                input_phase.assign(channels, std::vector<float>(bcap, 0.0f));
                output_phase.assign(channels, std::vector<float>(bcap, 0.0f));
                input_write_pos = 0;
                output_read_pos = 0;
                samples_since_last_fft = 0;
                need_phase_reset = true;
            }
            if (size_changed || new_overlap != overlap || new_window != window_type) {
                overlap = new_overlap;
                window_type = new_window;
                build_pitch_window(window_type, fft_size, window);
                window_scale = pitch_window_scale(window, overlap);
                hop_size = fft_size / std::max(1, overlap);
                // A live Hop/Window change alters the analysis/overlap geometry.
                // A size change already zeroed the buffers + reset the read/write
                // cursors above; but an overlap/window-only change would otherwise
                // jump output_write_pos while the ring still holds the previous
                // overlap-add tail and output_read_pos/phase are mid-stream — an
                // audible click. Resync the streaming state so the switch is clean.
                if (!size_changed) {
                    for (auto& b : input_buffer)  std::fill(b.begin(), b.end(), 0.0f);
                    for (auto& b : output_buffer) std::fill(b.begin(), b.end(), 0.0f);
                    for (auto& b : input_phase)   std::fill(b.begin(), b.end(), 0.0f);
                    for (auto& b : output_phase)  std::fill(b.begin(), b.end(), 0.0f);
                    input_write_pos = 0;
                    output_read_pos = 0;
                    samples_since_last_fft = 0;
                    need_phase_reset = true;
                }
                output_write_pos = std::min(hop_size, std::max(1, output_len) - 1);
            }
        }

        void reset() {
            for (auto& b : input_buffer)  std::fill(b.begin(), b.end(), 0.0f);
            for (auto& b : output_buffer) std::fill(b.begin(), b.end(), 0.0f);
            for (auto& b : input_phase)   std::fill(b.begin(), b.end(), 0.0f);
            for (auto& b : output_phase)  std::fill(b.begin(), b.end(), 0.0f);
            input_write_pos = 0;
            output_read_pos = 0;
            output_write_pos = std::min(hop_size, std::max(1, output_len) - 1);
            samples_since_last_fft = 0;
            need_phase_reset = true;
        }
    };

    float sample_rate_ = 48000.0f;
    PhaseVocoder pv_{};
};

inline std::unique_ptr<format::Processor> create_pitch_shift() {
    return std::make_unique<PitchShiftProcessor>();
}

} // namespace pulp::examples::classic

// Pulls in the inline definition of build_pitch_shift_editor (declared above) so
// the create_view() override links in every TU that uses the processor — the
// plugin adapter and the headless tests alike. After the class so the editor
// header sees a complete definition; its re-include of this file is a no-op.
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
#include "pitch_shift_editor.hpp"
#endif
