#pragma once

// Robotization / Whisperization — STFT phase-manipulation effect.
//
// Clean-room textbook implementation of the phase-vocoder effect from Reiss &
// McPherson, "Audio Effects: Theory, Implementation and Application" (the
// "Robotization/Whisperization" chapter): run a short-time Fourier transform
// over the signal, alter the per-bin phase, and resynthesize by weighted
// overlap-add.
//
//   * Pass-Through  — leave the spectrum untouched (delayed identity).
//   * Robotization  — set every bin's phase to zero (keep magnitude). Every
//                     analysis frame then restarts in phase at the analysis-hop
//                     rate, so the output takes on a steady monotone pitch at
//                     sample_rate / hop regardless of the input pitch, while the
//                     magnitude spectrum (the formants) is preserved.
//   * Whisperization — randomize every bin's phase (keep magnitude). The
//                     destroyed phase coherence turns voiced material into a
//                     breathy, unpitched whisper.
//
// FFT size, hop (overlap) and analysis window are all selectable at runtime,
// matching the book's reference plugin and the Rust/JUCE reference ports. The
// DSP is built directly on Pulp's own pulp::signal::Fft radix-2 transform with a
// hand-rolled ring-buffer STFT (analysis window only + scaled overlap-add, per
// the book); no third-party effect source was copied. See the README.

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
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pulp::examples::classic {

enum RobotizationParams : state::ParamID {
    kEffect  = 1,   // 0 Pass-Through, 1 Robotization, 2 Whisperization
    kRobotFftSize = 2,   // 0..7  -> 32, 64, 128, 256, 512, 1024, 2048, 4096
    kRobotHop     = 3,   // 0..2  -> overlap 2 (1/2), 4 (1/4), 8 (1/8)
    kRobotWindow  = 4,   // 0..3  -> Rectangular, Bartlett, Hann, Hamming
};

// Defined out-of-line in robotization_editor.hpp (included at the bottom).
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
std::unique_ptr<view::View> build_robotization_editor(state::StateStore& store);
#endif

class RobotizationProcessor : public format::Processor {
public:
// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
    std::unique_ptr<view::View> create_view() override { return build_robotization_editor(state()); }
#endif

    enum class Effect { PassThrough = 0, Robotization = 1, Whisperization = 2, Count };
    enum class Window { Rectangular = 0, Bartlett = 1, Hann = 2, Hamming = 3, Count };

    static constexpr int kNumSizes  = 8;
    static constexpr int kNumHops   = 3;
    static constexpr int kMaxFft    = 4096;
    static constexpr int kChannels  = 2;

    // Dropdown index -> concrete value. Labels live in the editor (see below).
    static constexpr int fft_size_for(int idx) {
        return 32 << std::clamp(idx, 0, kNumSizes - 1);   // 32 .. 4096
    }
    static constexpr int overlap_for(int idx) {
        switch (std::clamp(idx, 0, kNumHops - 1)) {
            case 0: return 2;   // 1/2
            case 1: return 4;   // 1/4
            default: return 8;  // 1/8
        }
    }

    // Option labels, shared by define_parameters() (count) and the editor.
    static const std::vector<std::string>& effect_labels() {
        static const std::vector<std::string> v{"Pass-Through", "Robotization", "Whisperization"};
        return v;
    }
    static const std::vector<std::string>& fft_size_labels() {
        static const std::vector<std::string> v{"32", "64", "128", "256",
                                                 "512", "1024", "2048", "4096"};
        return v;
    }
    static const std::vector<std::string>& hop_labels() {
        static const std::vector<std::string> v{"1/2", "1/4", "1/8"};
        return v;
    }
    static const std::vector<std::string>& window_labels() {
        static const std::vector<std::string> v{"Rectangular", "Bartlett", "Hann", "Hamming"};
        return v;
    }

    format::PluginDescriptor descriptor() const override {
        return {.name = "Robotization", .manufacturer = "Pulp Examples",
                .bundle_id = "com.pulp.examples.robotization", .version = "0.1.0",
                .category = format::PluginCategory::Effect,
                .input_buses = {{"Audio In", 2}}, .output_buses = {{"Audio Out", 2}}};
    }

    void define_parameters(state::StateStore& store) override {
        auto stepped = [](int count, float def) {
            return state::ParamRange{0.0f, static_cast<float>(count - 1), def, 1.0f};
        };
        // Defaults mirror the book's reference plugin / the Rust + JUCE ports:
        // Robotization, 512-pt FFT, 1/8 hop, Hann window.
        store.add_parameter({.id = kEffect,  .name = "Effect",   .unit = "",
                             .range = stepped(static_cast<int>(Effect::Count), 1.0f)});
        store.add_parameter({.id = kRobotFftSize, .name = "FFT Size", .unit = "",
                             .range = stepped(kNumSizes, 4.0f)});
        store.add_parameter({.id = kRobotHop,     .name = "Hop",      .unit = "",
                             .range = stepped(kNumHops, 2.0f)});
        store.add_parameter({.id = kRobotWindow,  .name = "Window",   .unit = "",
                             .range = stepped(static_cast<int>(Window::Count), 2.0f)});
    }

    void prepare(const format::PrepareContext& ctx) override {
        sample_rate_ = static_cast<float>(ctx.sample_rate);
        // Pre-create one FFT per selectable size so switching the FFT Size
        // dropdown never allocates on the audio thread.
        for (int i = 0; i < kNumSizes; ++i)
            fft_[i] = std::make_unique<signal::Fft>(fft_size_for(i));
        for (auto& r : input_ring_)  r.fill(0.0f);
        for (auto& r : output_ring_) r.fill(0.0f);
        // Force a (re)configure on the first process block.
        size_idx_ = hop_idx_ = window_idx_ = -1;
        configure(/*fft_idx=*/4, /*hop_idx=*/2, /*window_idx=*/2);
    }

    void release() override { reset_state(); }

    // Algorithmic latency of the ring-buffer overlap-add path is exactly the
    // FFT size: an input sample's reconstruction is only complete once every
    // overlapping analysis frame that covers it has been synthesized, which
    // trails the input by one full FFT frame. Block-size independent.
    int latency_samples() const override { return active_fft_size_; }

    void process(audio::BufferView<float>& output,
                 const audio::BufferView<const float>& input,
                 midi::MidiBuffer&, midi::MidiBuffer&,
                 const format::ProcessContext& ctx) override {
        const std::size_t in_ch  = input.num_channels();
        const std::size_t out_ch = output.num_channels();
        const int frames = static_cast<int>(output.num_samples());

        if (ctx.should_reset_dsp_state()) reset_state();

        // Adopt any dropdown changes (rebuilds window / hop, no allocation).
        configure(static_cast<int>(state().get_value(kRobotFftSize)),
                  static_cast<int>(state().get_value(kRobotHop)),
                  static_cast<int>(state().get_value(kRobotWindow)));

        const auto effect = static_cast<Effect>(
            std::clamp(static_cast<int>(state().get_value(kEffect)), 0,
                       static_cast<int>(Effect::Count) - 1));

        const int channels = static_cast<int>(std::min<std::size_t>(out_ch, kChannels));

        // The STFT positions are shared across channels (one analysis cadence
        // for the whole group), so snapshot them and restart each channel from
        // the same state, exactly like the reference port.
        const int saved_in    = input_write_pos_;
        const int saved_out_r = output_read_pos_;
        const int saved_out_w = output_write_pos_;
        const int saved_count = samples_since_fft_;

        for (int ch = 0; ch < channels; ++ch) {
            input_write_pos_  = saved_in;
            output_read_pos_  = saved_out_r;
            output_write_pos_ = saved_out_w;
            samples_since_fft_ = saved_count;

            const auto src = (static_cast<std::size_t>(ch) < in_ch) ? ch : 0;
            const float* in = (in_ch > 0) ? input.channel(static_cast<std::size_t>(src)).data()
                                          : nullptr;
            float* out = output.channel(static_cast<std::size_t>(ch)).data();
            float* iring = input_ring_[static_cast<std::size_t>(ch)].data();
            float* oring = output_ring_[static_cast<std::size_t>(ch)].data();
            const int n = active_fft_size_;

            for (int i = 0; i < frames; ++i) {
                iring[input_write_pos_] = in ? in[i] : 0.0f;
                if (++input_write_pos_ >= n) input_write_pos_ = 0;

                out[i] = oring[output_read_pos_];
                oring[output_read_pos_] = 0.0f;
                if (++output_read_pos_ >= n) output_read_pos_ = 0;

                if (++samples_since_fft_ >= hop_size_) {
                    samples_since_fft_ = 0;
                    process_frame(effect, ch);
                    output_write_pos_ += hop_size_;
                    if (output_write_pos_ >= n) output_write_pos_ -= n;
                }
            }
        }
        for (std::size_t ch = static_cast<std::size_t>(channels); ch < out_ch; ++ch) {
            auto o = output.channel(ch);
            for (int i = 0; i < frames; ++i) o[i] = 0.0f;
        }
    }

private:
    // One analysis -> modify -> synthesis frame for a single channel.
    void process_frame(Effect effect, int ch) {
        const int n = active_fft_size_;
        const float* iring = input_ring_[static_cast<std::size_t>(ch)].data();
        // Analysis: window the most recent N samples (oldest first), starting at
        // the input write head.
        int idx = input_write_pos_;
        for (int i = 0; i < n; ++i) {
            time_[static_cast<std::size_t>(i)] = window_[static_cast<std::size_t>(i)] * iring[idx];
            if (++idx >= n) idx = 0;
        }
        fft_[static_cast<std::size_t>(size_idx_)]->forward_real(time_.data(), freq_.data());
        modify(effect, n);
        fft_[static_cast<std::size_t>(size_idx_)]->inverse(freq_.data());

        // Synthesis: scaled overlap-add (no synthesis window — the analysis
        // window plus the book's window scale already give unity reconstruction
        // for a COLA-satisfying window/hop).
        float* oring = output_ring_[static_cast<std::size_t>(ch)].data();
        int o = output_write_pos_;
        for (int i = 0; i < n; ++i) {
            oring[o] += freq_[static_cast<std::size_t>(i)].real() * window_scale_;
            if (++o >= n) o = 0;
        }
    }

    // Per-bin spectral modification. freq_ holds the full N-point conjugate-
    // symmetric spectrum; we keep that symmetry so the inverse stays real.
    void modify(Effect effect, int n) {
        switch (effect) {
            case Effect::PassThrough:
                break;
            case Effect::Robotization:
                // Zero every bin's phase -> output spectrum is real and even.
                for (int k = 0; k < n; ++k) {
                    const float mag = std::abs(freq_[static_cast<std::size_t>(k)]);
                    freq_[static_cast<std::size_t>(k)] = {mag, 0.0f};
                }
                break;
            case Effect::Whisperization: {
                // Random per-bin phase. DC and Nyquist must stay real to keep
                // the inverse real; mirror bins carry the conjugate.
                const int nyq = n / 2;
                freq_[0] = {std::abs(freq_[0]), 0.0f};
                if (nyq > 0)
                    freq_[static_cast<std::size_t>(nyq)] =
                        {std::abs(freq_[static_cast<std::size_t>(nyq)]), 0.0f};
                for (int k = 1; k < nyq; ++k) {
                    const float mag = std::abs(freq_[static_cast<std::size_t>(k)]);
                    const float phase = 6.28318530717958648f * next_rand01();
                    const std::complex<float> v{mag * std::cos(phase), mag * std::sin(phase)};
                    freq_[static_cast<std::size_t>(k)] = v;
                    freq_[static_cast<std::size_t>(n - k)] = std::conj(v);
                }
                break;
            }
            default:
                break;
        }
    }

    // Rebuild window/hop state when a dropdown changes. No-op when unchanged, so
    // steady-state processing never recomputes. Allocation-free.
    void configure(int fft_idx, int hop_idx, int window_idx) {
        fft_idx    = std::clamp(fft_idx, 0, kNumSizes - 1);
        hop_idx    = std::clamp(hop_idx, 0, kNumHops - 1);
        window_idx = std::clamp(window_idx, 0, static_cast<int>(Window::Count) - 1);
        if (fft_idx == size_idx_ && hop_idx == hop_idx_ && window_idx == window_idx_)
            return;

        size_idx_ = fft_idx;
        hop_idx_  = hop_idx;
        window_idx_ = window_idx;
        active_fft_size_ = fft_size_for(fft_idx);
        const int overlap = overlap_for(hop_idx);
        hop_size_ = std::max(1, active_fft_size_ / overlap);

        build_window(static_cast<Window>(window_idx), active_fft_size_);

        // Window scale (book formula): fft_size / overlap / sum(window).
        double sum = 0.0;
        for (int i = 0; i < active_fft_size_; ++i) sum += window_[static_cast<std::size_t>(i)];
        window_scale_ = (sum > 0.0)
            ? static_cast<float>(active_fft_size_) / static_cast<float>(overlap)
                  / static_cast<float>(sum)
            : 0.0f;

        // A clean restart keeps the reported latency exact: read head at 0,
        // write head one hop ahead, rings cleared.
        reset_state();
    }

    void build_window(Window type, int n) {
        switch (type) {
            case Window::Rectangular:
                for (int i = 0; i < n; ++i) window_[static_cast<std::size_t>(i)] = 1.0f;
                break;
            case Window::Bartlett: {
                const float denom = static_cast<float>(std::max(1, n - 1));
                for (int i = 0; i < n; ++i)
                    window_[static_cast<std::size_t>(i)] =
                        1.0f - std::fabs(2.0f * static_cast<float>(i) / denom - 1.0f);
                break;
            }
            case Window::Hann: {
                const float denom = static_cast<float>(std::max(1, n - 1));
                for (int i = 0; i < n; ++i)
                    window_[static_cast<std::size_t>(i)] =
                        0.5f - 0.5f * std::cos(6.28318530717958648f * static_cast<float>(i) / denom);
                break;
            }
            case Window::Hamming: {
                const float denom = static_cast<float>(std::max(1, n - 1));
                for (int i = 0; i < n; ++i)
                    window_[static_cast<std::size_t>(i)] =
                        0.54f - 0.46f * std::cos(6.28318530717958648f * static_cast<float>(i) / denom);
                break;
            }
            default:
                break;
        }
    }

    void reset_state() {
        for (auto& r : input_ring_)  r.fill(0.0f);
        for (auto& r : output_ring_) r.fill(0.0f);
        input_write_pos_  = 0;
        output_read_pos_  = 0;
        output_write_pos_ = std::min(hop_size_, std::max(1, active_fft_size_) - 1);
        samples_since_fft_ = 0;
    }

    // Cheap, deterministic xorshift32 — RT-safe, no allocation, no global state.
    float next_rand01() {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return static_cast<float>(rng_ & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }

    float sample_rate_ = 48000.0f;

    std::array<std::unique_ptr<signal::Fft>, kNumSizes> fft_{};
    std::array<std::array<float, kMaxFft>, kChannels> input_ring_{};
    std::array<std::array<float, kMaxFft>, kChannels> output_ring_{};
    std::array<float, kMaxFft> window_{};
    std::array<float, kMaxFft> time_{};
    std::array<std::complex<float>, kMaxFft> freq_{};

    int size_idx_ = -1, hop_idx_ = -1, window_idx_ = -1;
    int active_fft_size_ = 512;
    int hop_size_ = 64;
    float window_scale_ = 1.0f;

    int input_write_pos_ = 0;
    int output_read_pos_ = 0;
    int output_write_pos_ = 0;
    int samples_since_fft_ = 0;
    std::uint32_t rng_ = 0x9E3779B9u;
};

inline std::unique_ptr<format::Processor> create_robotization() {
    return std::make_unique<RobotizationProcessor>();
}

} // namespace pulp::examples::classic

// Editor-only: excluded from headless WASM DSP builds (see PULP_HEADLESS).
#if !PULP_HEADLESS
#include "robotization_editor.hpp"
#endif
