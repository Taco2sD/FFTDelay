# FFTDelay

[![CI](https://github.com/Taco2sD/FFTDelay/actions/workflows/ci.yml/badge.svg)](https://github.com/Taco2sD/FFTDelay/actions/workflows/ci.yml)

A real-time **spectral delay** plugin built with [JUCE](https://juce.com) 8,
where delay, feedback and attenuation are painted *per frequency band* rather
than set as single global values.

Builds as **VST3**, **AU**, and a **Standalone** app from one codebase.

## What it does

Audio is analysed with an STFT; each frequency band can be given its own delay
time, feedback and gain by drawing curves across the spectrum. On top of that
core sit modulation, frequency-domain "treatments", full per-band stereo, a
preset system and complete MIDI Learn.

### Controls

**The three matrices** (painted per-band curves, drag to draw, double-click to reset):
- **Attenuation** — per-band gain (spectral filter)
- **Delay** — per-band delay time
- **Feedback** — per-band feedback

Both L and R channels are shown at once (six bars, two columns). **Link L/R**
mirrors edits; **Copy L→R / R→L** copy one channel onto the other. A live
**spectral analyzer** is drawn behind each curve, aligned to the edit-mask axis.

**Analysis:** FFT size (64–1024), Overlap (none/2×/3×), Window
(Hann/Triangle/Hamming/Rectangle), Edit Mask (Linear / Adapted 2× / 4× / Log).

**Delay:** master Delay + Delay Range (375 ms…12 s); tempo **Sync** to a grid
(¼, ⅛, ⅛T, 1⁄16) with **Auto Quantize**; **Smooth** to smear the matrices.

**LFO:** on/off, 6 waveforms (sine/triangle/saw up/saw down/square/random),
free rate (Hz) or tempo **Sync** (1/1…1/16), and 3 routings → Attenuation /
Delay / Feedback / Dry-Wet / Effect Gain / Matrix X (freq) / Matrix Y (level),
each with bipolar depth.

**Input Modulation:** one of 11 frequency-domain algorithms (Deterioration,
Jello Mold, PitchRoll, Smear, TimeSponge, Foam, Lime Twist, Horse Tail,
Mini-Pulses, Imprint, Phase Blaster), per channel with an **IM Link**. The three
IM knobs relabel to the chosen algorithm's controls.

**Output:** Dry/Wet (latency-compensated), Effect Gain (±24 dB), Mute, Bypass,
and a **Freeze** that captures the current spectrum and sustains it.

**Presets:** save/recall to `~/Documents/FFTDelay/Presets`, with `<` / `>`
navigation and a factory bank (Init, Spectral Echo, Freeze Pad, Hi-Band
Shimmer, Smear Wash).

**MIDI Learn (Omnisphere-style):** right-click *any* parameter → Learn / Clear /
Invert; the **MIDI** button opens a manager with per-assignment channel, value
range and invert. **Preset Next/Previous** are learnable actions too. All
assignments are saved with the plugin state.

## Building

Requires CMake 3.22+ and a C++ compiler (Xcode on macOS).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The first configure downloads JUCE, so it takes a while; later builds are fast.
With `COPY_PLUGIN_AFTER_BUILD` on, the VST3/AU are installed to your user plugin
folders automatically — rescan in your DAW to see "FFTDelay".

Run the standalone directly:

```sh
open ./build/FFTDelay_artefacts/Release/Standalone/FFTDelay.app
```

> **Note:** because the plugin receives MIDI (for MIDI Learn), the Audio Unit
> registers as a **music effect** (`aumf`), not a plain effect (`aufx`). It may
> appear under MIDI-controlled / instrument-FX categories in some hosts.
> Validate with `auval -v aumf Fdly Rdcl`.

## Layout

```
CMakeLists.txt            Build config; pulls JUCE via FetchContent
source/
  PluginProcessor.*       Audio engine + parameters (audio thread)
  PluginEditor.*          GUI: matrix editors, controls, MIDI Learn panel
  SpectralDelay.*         The per-band STFT delay engine + Input Modulation
  SpectralCurves.h        Lock-free per-band curve store (UI <-> audio)
  PresetManager.h         On-disk preset save/recall + navigation
  MidiLearnManager.h      Lock-free CC -> parameter/action assignments
docs/
  architecture.md              Design / architecture overview
```

## Testing

Unit tests cover the curve store, the MIDI-Learn assignment map, and the
spectral engine's numeric sanity (no NaNs, silence-in/silence-out, RT-safe FFT
resizing), using JUCE's `UnitTest` framework and registered with CTest:

```sh
cmake --build build --target FFTDelayTests
ctest --test-dir build --output-on-failure
```

CI (GitHub Actions, macOS) builds all formats and runs the tests on every push.

## Status

Feature-complete; builds clean (VST3/AU/Standalone), unit-tested, and passes
`auval`. See [docs/architecture.md](docs/architecture.md) for a design overview.

## License

Proprietary — © 2026 RadicallyDecent, all rights reserved (see
[LICENSE](LICENSE)). Built against the JUCE framework, which is licensed
separately; distributing a binary requires a valid JUCE licence.
