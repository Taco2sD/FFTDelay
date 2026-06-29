# Changelog

All notable changes to FFTDelay are documented here. The format loosely follows
[Keep a Changelog](https://keepachangelog.com/); the project uses semantic
versioning once it reaches 1.0.

## [Unreleased]

### Added
- **Per-band spectral engine** — Attenuation / Delay / Feedback matrices as
  painted 160-point curves, with selectable Edit Mask (Linear / Adapted 2× / 4× /
  Logarithmic).
- **Analysis controls** — FFT size (64–1024), overlap (none/2×/3×), window
  (Hann / Triangle / Hamming / Rectangle), all real-time safe.
- **Delay** — selectable range (375 ms–12 s), tempo Sync to a grid
  (¼/⅛/⅛T/1⁄16) with Auto Quantize, and Smooth.
- **Freeze**, and an output section: Dry/Wet (latency-compensated), Effect Gain
  (±24 dB), Mute, Bypass.
- **LFO** — 6 waveforms, free/tempo-synced rate, 3 routings (incl. Matrix X/Y).
- **Input Modulation** — all 11 algorithms, per channel with an IM Link; IM
  knobs relabel to the selected algorithm's controls.
- **Stereo** — per-channel matrices shown side by side, Link L/R, Copy L↔R.
- **Presets** — on-disk save/recall with next/previous and a factory bank.
- **MIDI Learn** — Omnisphere-style: right-click any parameter, per-assignment
  channel/range/invert, a manager window, and learnable Preset Next/Previous.
- **Live spectral analyzer** behind the matrix curves; dark custom LookAndFeel.
- **Unit tests** (JUCE UnitTest + CTest) and **CI** (GitHub Actions, macOS).

### Notes
- Because the plugin receives MIDI (for MIDI Learn), the Audio Unit registers as
  a music effect (`aumf`); validate with `auval -v aumf Fdly Rdcl`.
