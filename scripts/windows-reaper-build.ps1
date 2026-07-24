param(
    [string]$PulpCommit = "99046803a651314a03ead5fc1384cfcf852ff7ce",
    [string]$ClassicCommit = "d62c2c83c31ecee7dbd78b3a53e68cb472b8382d",
    [string]$WorkRoot = "C:\pulp-classic-effects-reaper",
    [switch]$ResumePulpBuild,
    [switch]$SkipPulpSdkBuild
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Program,
        [Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments
    )
    Write-Host ">> $Program $($Arguments -join ' ')"
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

function Sync-Repository {
    param(
        [string]$Url,
        [string]$Path,
        [string]$Commit
    )
    if (-not (Test-Path (Join-Path $Path ".git"))) {
        Invoke-Checked git clone --filter=blob:none $Url $Path
    }
    Invoke-Checked git -C $Path fetch --quiet origin
    Invoke-Checked git -C $Path reset --hard $Commit
    Invoke-Checked git -C $Path clean -ffd
}

New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null

$vcvars = (
    Get-ChildItem "C:\Program Files\Microsoft Visual Studio" -Recurse `
        -Filter vcvarsall.bat -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "BuildTools" } |
    Select-Object -First 1
).FullName
if (-not $vcvars) {
    throw "Visual Studio Build Tools vcvarsall.bat was not found"
}

# Import the x64 developer environment into this PowerShell process. The VM is
# Windows ARM64, but the published Pulp Skia/Dawn archive and REAPER smoke host
# are x64, executed through Windows 11's built-in x64 compatibility layer.
$devEnvironment = & cmd.exe /d /c "`"$vcvars`" x64 >nul && set"
if ($LASTEXITCODE -ne 0) {
    throw "vcvarsall x64 failed with exit code $LASTEXITCODE"
}
foreach ($line in $devEnvironment) {
    if ($line -match "^([^=]+)=(.*)$") {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
    }
}
Write-Host "Compiler: $((Get-Command cl.exe).Source)"

$pulp = Join-Path $WorkRoot "pulp"
$classic = Join-Path $WorkRoot "pulp-classic-effects"
$sdk = Join-Path $WorkRoot "pulp-sdk"
$pulpBuild = Join-Path $WorkRoot "build-pulp"
$classicBuild = Join-Path $WorkRoot "build-classic-effects"
$fetchContentBuild = "C:\pfc-x64"
$artifacts = Join-Path $WorkRoot "artifacts"

# A retry must never reuse a generator/architecture from an interrupted run.
# Pulp otherwise puts FetchContent sub-builds under %LOCALAPPDATA%\Pulp\fc,
# outside the selected CMake binary directory.
$generatedPaths = @($classicBuild)
if (-not $SkipPulpSdkBuild) {
    $generatedPaths += $sdk
}
if (-not $ResumePulpBuild) {
    $generatedPaths += @($pulpBuild, $fetchContentBuild)
}
foreach ($generated in $generatedPaths) {
    if (Test-Path $generated) {
        Remove-Item $generated -Recurse -Force
    }
}
[Environment]::SetEnvironmentVariable(
    "PULP_FETCHCONTENT_BASE_DIR",
    $fetchContentBuild,
    "Process"
)

Sync-Repository `
    -Url "https://github.com/Generous-Corp/pulp.git" `
    -Path $pulp `
    -Commit $PulpCommit
Sync-Repository `
    -Url "https://github.com/danielraffel/pulp-classic-effects.git" `
    -Path $classic `
    -Commit $ClassicCommit
Sync-Repository `
    -Url "https://github.com/steinbergmedia/vst3sdk.git" `
    -Path (Join-Path $pulp "external\vst3sdk") `
    -Commit "v3.8.0_build_66"
Invoke-Checked git -C (Join-Path $pulp "external\vst3sdk") `
    submodule update --init --recursive

# Pulp's chrome/m151 release has a published Windows x64 Skia+Dawn archive,
# although the current release manifest does not automatically fetch it.
$skiaUrl = "https://github.com/danielraffel/skia-builder/releases/download/" +
           "chrome/m151/skia-build-win-x64-gpu-release.zip"
$skiaSha256 = "c9aa8a8e350000e7b5e378369737abc4bf6ed42c03e66f9ec0042555d98e174a"
$skiaZip = Join-Path $WorkRoot "skia-build-win-x64-gpu-release.zip"
if (-not (Test-Path $skiaZip) -or
    (Get-FileHash $skiaZip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $skiaSha256) {
    Invoke-Checked curl.exe -L --fail --retry 4 --output $skiaZip $skiaUrl
}
$actualSkiaSha = (Get-FileHash $skiaZip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualSkiaSha -ne $skiaSha256) {
    throw "Skia archive SHA-256 mismatch: expected $skiaSha256, got $actualSkiaSha"
}

$skiaRoot = Join-Path $pulp "external\skia-build"
Expand-Archive -Path $skiaZip -DestinationPath $skiaRoot -Force
$skiaRelease = Join-Path $skiaRoot "build\win-gpu\lib\Release"
$skiaArchRelease = Join-Path $skiaRelease "x64"
if (Test-Path $skiaArchRelease) {
    Copy-Item (Join-Path $skiaArchRelease "*") $skiaRelease -Recurse -Force
}
foreach ($required in @("skia.lib", "dawn_combined.lib")) {
    if (-not (Test-Path (Join-Path $skiaRelease $required))) {
        throw "Published Skia archive did not provide $required"
    }
}

$pulpConfigureArguments = @(
    "-S", $pulp,
    "-B", $pulpBuild,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DPULP_ENABLE_GPU=ON",
    "-DPULP_REQUIRE_GPU_FOR_SDK=ON",
    "-DPULP_BUILD_TESTS=OFF",
    "-DPULP_BUILD_EXAMPLES=OFF",
    "-DPULP_BUILD_WEBVIEW=OFF",
    "-DCMAKE_SUPPRESS_REGENERATION=ON",
    "-DSKIA_DIR=$skiaRoot",
    "-DCMAKE_INSTALL_PREFIX=$sdk"
)
if (-not $ResumePulpBuild) {
    Invoke-Checked -Program cmake -Arguments $pulpConfigureArguments
} elseif (-not (Test-Path (Join-Path $pulpBuild "CMakeCache.txt"))) {
    throw "Cannot resume because the Pulp CMake cache does not exist"
}
# Build the SDK surface rather than the top-level ALL target.  The latter also
# builds authoring tools such as pulp-import-design, which are not part of the
# plugin SDK and can require Chromium support libraries not shipped in the
# public Skia archive.
$pulpSdkTargets = @(
    "pulp-platform", "pulp-runtime", "pulp-timebase", "pulp-timeline",
    "pulp-playback", "pulp-events", "pulp-state", "pulp-audio", "pulp-midi",
    "pulp-signal", "pulp-graph", "pulp-format", "pulp-osc", "pulp-canvas",
    "pulp-view-core", "pulp-view", "pulp-view-script", "pulp-standalone",
    "pulp-dsl", "pulp-native-components", "pulp-signal-fft-backend",
    "pulp-signal-modal-spec", "pulp-host", "pulp-render", "pulp-gpu-audio",
    "pulp-inspect", "pulp-bundled-fonts", "vst3-sdk", "yogacore", "hwy",
    "SDL3-static"
)
if (-not $SkipPulpSdkBuild) {
    foreach ($target in $pulpSdkTargets) {
        Invoke-Checked -Program cmake -Arguments @(
            "--build", $pulpBuild, "--config", "Release", "--parallel", "8",
            "--target", $target
        )
    }

    # Install only rules declared in Pulp's root CMakeLists.  This includes the
    # complete SDK export, headers, dependencies, and CMake package files while
    # skipping subdirectory install rules for unrelated top-level executables.
    Invoke-Checked -Program cmake -Arguments @(
        "-DCMAKE_INSTALL_LOCAL_ONLY=ON",
        "-DCMAKE_INSTALL_PREFIX=$sdk",
        "-DCMAKE_INSTALL_CONFIG_NAME=Release",
        "-P", (Join-Path $pulpBuild "cmake_install.cmake")
    )

    # Dependency package metadata is owned by subdirectory install rules.
    # Populate it with a normal install pass. Top-level developer executables
    # are intentionally not built above, so that pass may stop when it reaches
    # one; by then the dependency packages needed by SDK consumers are present.
    & cmake --install $pulpBuild --config Release --prefix $sdk
    $dependencyConfig = Join-Path $sdk "lib\cmake\SheenBidi\SheenBidiConfig.cmake"
    if (-not (Test-Path $dependencyConfig)) {
        throw "Pulp dependency package installation did not produce $dependencyConfig"
    }
} elseif (-not (Get-ChildItem $sdk -Recurse -Filter "PulpConfig.cmake" `
                    -ErrorAction SilentlyContinue | Select-Object -First 1)) {
    throw "Cannot skip the Pulp SDK build because the installed SDK is missing"
}

$pulpCache = Get-Content (Join-Path $pulpBuild "CMakeCache.txt")
foreach ($expected in @(
    "PULP_ENABLE_GPU:BOOL=ON",
    "PULP_REQUIRE_GPU_FOR_SDK:BOOL=ON"
)) {
    if ($pulpCache -notcontains $expected) {
        throw "Pulp CMake cache is missing required entry: $expected"
    }
}

$classicConfigureArguments = @(
    "-S", $classic,
    "-B", $classicBuild,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DPULP_BUILD_TESTS=OFF",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
    "-DCMAKE_SUPPRESS_REGENERATION=ON",
    "-DCMAKE_PREFIX_PATH=$sdk"
)
Invoke-Checked -Program cmake -Arguments $classicConfigureArguments
Invoke-Checked -Program cmake -Arguments @(
    "--build", $classicBuild, "--config", "Release", "--parallel", "8"
)

$vst3Root = Join-Path $classicBuild "VST3-bundles"
New-Item -ItemType Directory -Force -Path $vst3Root | Out-Null
$vst3Modules = @(
    Get-ChildItem (Join-Path $classicBuild "VST3\Release") `
        -File -Filter "*.dll" |
    Where-Object { $_.Name -ne "wgpu_native.dll" }
)
$wgpuRuntime = Join-Path $classicBuild "VST3\Release\wgpu_native.dll"
foreach ($module in $vst3Modules) {
    $bundle = Join-Path $vst3Root ($module.BaseName + ".vst3")
    $architectureDir = Join-Path $bundle "Contents\x86_64-win"
    New-Item -ItemType Directory -Force -Path $architectureDir | Out-Null
    Copy-Item $module.FullName `
        (Join-Path $architectureDir ($module.BaseName + ".vst3")) -Force
    if (Test-Path $wgpuRuntime) {
        Copy-Item $wgpuRuntime $architectureDir -Force
    }
}
$bundles = @(
    Get-ChildItem $vst3Root -Directory -Filter "*.vst3" -ErrorAction SilentlyContinue
)
if ($bundles.Count -lt 15) {
    throw "Expected at least 15 classic-effects VST3 bundles, found $($bundles.Count)"
}

$systemVst3 = Join-Path $env:CommonProgramFiles "VST3"
New-Item -ItemType Directory -Force -Path $systemVst3 | Out-Null
foreach ($bundle in $bundles) {
    $destination = Join-Path $systemVst3 $bundle.Name
    if (Test-Path $destination) {
        Remove-Item $destination -Recurse -Force
    }
    Copy-Item $bundle.FullName $destination -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$zipPath = Join-Path $artifacts "pulp-classic-effects-windows-x64-vst3.zip"
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}
Compress-Archive -Path (Join-Path $vst3Root "*") -DestinationPath $zipPath

$manifest = [ordered]@{
    pulp_commit = $PulpCommit
    classic_effects_commit = $ClassicCommit
    architecture = "x64-on-Windows-11-ARM64"
    configuration = "Release"
    skia_sha256 = $actualSkiaSha
    vst3_count = $bundles.Count
    vst3_bundles = @($bundles.Name | Sort-Object)
    installed_to = $systemVst3
    archive = $zipPath
}
$manifest | ConvertTo-Json -Depth 4 |
    Set-Content (Join-Path $artifacts "build-manifest.json") -Encoding UTF8

Write-Host "BUILD_COMPLETE"
$manifest | ConvertTo-Json -Depth 4
