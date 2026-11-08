# OpenRB Delay

A JUCE VST3 / AU / standalone effect that wraps the ReBirth delay from
[OpenRB-338](https://github.com/pingw33n/openrb-338). The DSP is a copy of `orb-engine`'s
`Delay` in `third_party/openrb-338`; this project does not rewrite it.

The effect always runs at 44100 Hz in 64-sample blocks, the same grid the original uses. Host
audio at any other rate is resampled in and out. Delay length is `length(tempo, steps, mode)`
with `tempo = BPM * 1000` from the host playhead. 120 BPM is used when the host has no tempo.
There is no pattern clock: the delay never reads song position or ticks.

The original delay is a send effect. This plugin is a dry-plus-wet insert: dry stereo passes
through, a mono send `T[mix] * 0.5 * (L + R)` feeds one delay line, and the panned tap is added
back. Mix is ReBirth's delay-send curve `T[]` (`T[127] = 1`, about `(23/24)^(127-i)` below that),
not a linear wet/dry. Mix 0 is dry; mix 127 is full send.

This plugin is GPL-3.0-only. JUCE is pulled under its AGPLv3. The engine copy is the
`orb-engine` and `rbs` crates only, under the same license. Copyright (C) 2026 the
OpenRB-338 authors. OpenRB-338 itself is not modified.

## Layout

```
third_party/openrb-338/crates/orb-engine   # copied engine
third_party/openrb-338/crates/rbs           # copied song-format crate the engine links
```

## Build

Rust 1.99.1 (see `rust-toolchain.toml`) and CMake 3.24+. macOS builds are universal
(`arm64` + `x86_64`) so native Ableton 12 on Apple Silicon can load the VST3. The first
build will ask `rustup` for `aarch64-apple-darwin` if that target is missing.

```sh
cargo test
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

VST3, AU, and the standalone app are copied into `output/` after the build. Nothing is copied
into the system plug-in folders.

```
output/VST3/OpenRB Delay.vst3
output/AU/OpenRB Delay.component
output/Standalone/OpenRB Delay.app
```

Those copies are ad-hoc signed and are fine on this Mac. To share a zip that Gatekeeper will
load on other machines (Intel and Apple Silicon), install a Developer ID Application
certificate and a notary profile, then:

```sh
./scripts/package-macos.sh
```

That rebuilds, signs with the Developer ID, notarizes, and writes
`output/OpenRB-Delay-macOS-vVERSION.zip`. Run `./scripts/package-macos.sh --help` for the
one-time `notarytool store-credentials` setup. `--skip-build` packages the current `output/`.

## Controls

| Control  | Range              | Notes                                         |
|----------|--------------------|-----------------------------------------------|
| Mix      | 0..127             | delay send via ReBirth `T[]`; default 0       |
| Feedback | 0..127             | regeneration; default 64                      |
| Pan      | 0..127             | tap pan; default 64 (centre)                  |
| Mode     | Triplet / Sixteenth| engine `0` is triplets; default Sixteenth (`1`) |
| Steps    | 1..32              | delay length in steps; default 3              |

At 44100 Hz the resamplers are bypassed, so the samples that reach `Delay` are the host samples.
