# FFTDelay — Architecture

A short map of how the plugin is put together. FFTDelay is a real-time spectral
delay: audio is analysed with a Short-Time Fourier Transform and every
frequency band gets its own delay, feedback and gain, painted as curves.

## Signal flow

```
input → STFT analysis → Input Mod → Attenuation → Delay → Feedback → iSTFT → Dry/Wet → Effect Gain → output
                                                                          ↑
                                   (dry path, latency-compensated) ───────┘
```

Each channel runs its own `SpectralDelay` engine producing the wet signal; the
processor mixes it against a sample-aligned dry path and applies the output
gain. The painted matrices ARE the parameters — there are no global scalers
behind them; each matrix has an automatable whole-curve value offset, an L/R
link, and a stage bypass instead.

## Components

| File | Role |
|------|------|
| `source/SpectralDelay.*` | The per-channel STFT engine: overlapping windowed frames (64–1024 bands, 0x/1x/2x overlap, four windows), a flat spectral ring pool holding a full 12 s at every setting (stride reinterpreted per frame size, zero audio-thread allocation), per-bin whole-frame delay taps with true zero-delay passthrough and inert feedback at zero, the eleven Input Mod treatments, Freeze, magnitude taps for the sonograms, and an alignment fifo that holds reported latency constant across FFT sizes. |
| `source/SpectralCurves.h` | The three painted per-band matrices as 160-point curves plus per-band selection flags, all lock-free (UI writes, audio reads). Includes the edit-mask mapping (Linear / Adapted 2x / 4x / Logarithmic) and the destructive curve ops (smooth, quantize, average, flips, waveform generator). |
| `source/PluginProcessor.*` | Parameters (APVTS), the block pipeline: MIDI Learn / factory CC map, LFO with 25 destinations ({matrix} x {channel} x {value X / frequency Y / selected bands} + the six Input Mod controls), tempo sync, curve→bin fan-out with offsets, dry/wet + output stage, presets, delay-memory housekeeping, state save/restore with a version migrator. |
| `source/PluginEditor.*` | The GUI on a fixed logical canvas scaled 75–200%: four section cards (GLOBAL / EDIT / LFO / ANALYSIS), tempo toolbar, the matrix field (L row top, R row bottom, graph-control strip between, IN/OUT sonograms flanking), the INPUT MOD card, and the MIDI Learn overlay. All colours/fonts come from `Theme.h`. |
| `source/Theme.h` | The design-token table: background ladder, per-matrix accent family, type ladder, metrics. |
| `source/PresetManager.h` | On-disk preset save/recall, next/previous, post-load hook. |
| `source/MidiLearnManager.h` | Lock-free CC → parameter/action map (channel, range, invert), factory defaults. |

## Key design choices

- **Curves as the source of truth.** The matrices hold the actual values;
  automation reaches them through whole-curve offsets and the LFO's per-matrix
  X/Y/Sel routes, which are applied at fan-out time and never redraw the curves.
- **Two-tier band model.** 160 edit columns fan onto up to 1024 analysis bins
  through the selected edit mask; the Adapted masks give the lowest bins 1:1
  columns and group the rest.
- **Constant reported latency** (one max-size frame, 2048 samples): smaller
  FFT sizes are padded up by an internal alignment delay, so switching sizes
  never renegotiates host latency and dry/wet stays sample-aligned. This is a
  deliberate trade against reporting the (smaller) per-setting minimum.
- **Lock-free UI↔audio sharing** throughout: curves, selection, spectra,
  tempo readout and MIDI Learn assignments are plain relaxed atomics.
- **Non-realtime work is marshalled off the audio thread** (preset recall via
  AsyncUpdater; delay-memory resets via a flag consumed at block start).

## Tests

`tests/SpectralTests.cpp` (JUCE `UnitTest`, run via CTest) covers the curve
store and its ops, selection, edit-mask mapping, the MIDI-Learn map, and the
engine's guarantees: constant latency at both frame extremes, zero-delay
passthrough, feedback inert at zero delay, full-cut silence, a 12 s painted
delay landing within one hop, and all eleven Input Mod algorithms staying
finite on live signal.
