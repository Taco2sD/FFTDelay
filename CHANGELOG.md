# Changelog

All notable changes to FFTDelay are documented here. The format loosely follows
[Keep a Changelog](https://keepachangelog.com/); the project uses semantic
versioning once it reaches 1.0.

## [0.2.0] — 2026-07-02 — the rework

### Changed
- **The painted matrices are now the parameters** (state version 2, with a
  migrator for v1 sessions): the global delay/feedback/attenuation scalers are
  replaced by per-matrix automatable offsets, per-matrix L/R links, and stage
  bypasses. Feedback reaches a true 100%; curve bottoms are a genuine full cut.
- **Engine fidelity:** real 1024-band mode (frame 2048), a spectral ring that
  holds a full 12 s at every FFT/overlap combination with zero audio-thread
  allocation, whole-frame delay quantisation, true zero-delay passthrough with
  inert feedback, and constant reported latency with internal alignment.
- **The LFO grew to 25 destinations:** value X / frequency Y / selected-bands
  Sel per matrix per channel, plus the six Input Mod controls.
- **Input Mod audit:** Scatter is a true per-band time displacement, Invert a
  level inversion, Pulse Gate recirculates through internal feedback; all
  algorithms carry original names (Erode, Invert, Bin Shift, Smear, Drift,
  Ripple, Scatter, Comb, Pulse Gate, Spectral Gate, Phase Scramble) and a
  finite-output regression test.
- **Complete UI rebuild** on a design-token theme: section cards (GLOBAL /
  EDIT / LFO / ANALYSIS / INPUT MOD), tempo toolbar with live BPM, the matrix
  field with L-top/R-bottom rows and a per-column graph-control strip,
  vertical-frequency editors with glow-edge rendering and tempo gridlines,
  shift-drag red band selection, IN/OUT sonograms, curve transformers and
  six-shape generators with undo, and free 75–200% vector resizing.

## [0.1.0] — 2026-06-29

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
