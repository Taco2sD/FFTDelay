# FFTDelay — Architecture

A short map of how the plugin is put together. FFTDelay is a real-time spectral
delay: it analyses audio with a Short-Time Fourier Transform and lets each
frequency band have its own delay, feedback and gain.

## Signal flow

```
input → STFT analysis → Input Modulation → Attenuation → Delay → Feedback → iSTFT → Dry/Wet → Effect Gain → output
                                                                                 ↑
                                          (dry path, latency-compensated) ───────┘
```

Each channel runs its own `SpectralDelay` engine producing the wet signal; the
processor mixes it against a latency-compensated dry path and applies the output
gain.

## Components

| File | Role |
|------|------|
| `source/SpectralDelay.*` | The per-channel STFT engine: overlapping windowed FFT frames, a ring buffer of past spectra (the delay line), per-bin delay tap / feedback / gain, the Input Modulation treatments, and the analysed magnitude spectrum for the UI. Real-time safe — no allocation or locks on the audio thread (all FFT sizes are pre-built; settings changes are applied at frame boundaries). |
| `source/SpectralCurves.h` | The three painted per-band "matrices" (Attenuation / Delay / Feedback) as rows of edit-band control points. Lock-free: the UI writes points, the audio thread reads them, via per-element relaxed atomics. |
| `source/PluginProcessor.*` | Parameters (APVTS), per-block parameter reads, the LFO, tempo sync, the dry/wet + output stage, preset generation, MIDI handling, and state save/restore. Maps the edit-band curves onto analysis bins through the chosen edit mask. |
| `source/PluginEditor.*` | The GUI: six paintable matrix editors (L/R × three), the control grid, the spectral-analyzer backdrop, the LookAndFeel, and the MIDI Learn panel. |
| `source/PresetManager.h` | On-disk preset save/recall and next/previous navigation. |
| `source/MidiLearnManager.h` | Lock-free CC → parameter/action assignment map (channel, range, invert), read on the audio thread. |

## Key design choices

- **Two-tier band model.** The user paints a coarse grid of edit bands; the
  processor fans those onto the (up to 1024) analysis bins via an edit mask
  (Linear / Adapted / Logarithmic), so the painted curves stay editable at any
  FFT size.
- **Constant reported latency.** Latency is reported as one max-size frame and
  held constant, so switching FFT size never renegotiates host latency.
- **Lock-free UI↔audio sharing.** Curves, the spectrum display and MIDI Learn
  assignments are all plain per-element atomics — the audio thread never blocks.
- **MIDI-triggered preset changes** are marshalled to the message thread
  (preset I/O is not real-time safe) via an `AsyncUpdater`.

## Tests

`tests/SpectralTests.cpp` (JUCE `UnitTest`, run via CTest) covers the curve
store, the MIDI-Learn map, and the engine's numeric sanity. See the README for
how to run them.
