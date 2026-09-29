# Feedback Suppressor (VST3/AU/Standalone)

A JUCE audio plugin combining four corrective DSP stages, in the same
problem space as tools like Alpha Labs' de:feedback:

1. **Adaptive feedback cancellation (AFC)** — an NLMS adaptive filter that
   learns the acoustic path from your loudspeakers to this mic (using a
   reference of the actual main-out feed) and subtracts the predicted
   leaked copy. This is *true* cancellation, the same family of algorithm
   used in acoustic echo cancellation, not a notch/EQ trick.
2. **Notch safety net** — continuously analyses the residual signal for
   narrow, persistent spectral peaks (the fingerprint of any feedback that
   gets past the AFC stage, e.g. right after AFC starts adapting) and clamps
   them with adaptive high-Q notch filters. Same family of technique as
   hardware "feedback destroyers" (Sabine FBX, dbx AFS).
3. **Denoise** — STFT spectral subtraction using a minimum-statistics noise
   floor tracker per frequency bin.
4. **De-reverb** — STFT spectral subtraction against a decaying "high-water
   mark" model of the reverberant tail per bin.

Signal flow per instance: `mic in → AFC → notch → denoise → de-reverb → output gain`.

## Plugin architecture: one instance per mic, mono + a Reference sidechain

The plugin's main bus is **mono** — insert one instance per choir mic
channel, the way you'd insert a gate or compressor on a channel strip. It
also declares a second, mono **aux input bus called "Reference"**, which
you route your main/FOH output feed into. Every mic instance uses that same
reference to model its own (different) acoustic path to the speakers.

How to connect the Reference bus depends on your host:
- **DAWs with sidechain routing** (Reaper, Ableton, Logic, etc.): insert the
  plugin, then use the host's sidechain input picker to route your main
  output bus (or an aux/matrix send carrying that identical signal) into
  the plugin's "Reference" input. In Reaper this is the routing matrix; in
  Ableton it's the sidechain dropdown at the bottom of the plugin window.
- **Live-sound software** (e.g. a DAW-based live console): route a post-fader
  aux send from your main output bus to the input channel the plugin sits
  on, mapped to its second input.
- Make sure the reference is the **actual signal driving the loudspeakers**
  (post main EQ/limiter if at all possible) — the closer it matches what's
  really coming out of the speakers, the better the model.

If no reference is connected, the AFC stage automatically passes audio
through unprocessed and the notch/denoise/de-reverb stages still work on
their own — you'll see "Reference: not connected" in the plugin window.

## Honesty about scope

This is a solid, from-scratch starting point using well-established, public
DSP techniques — **not** a reverse-engineering of Alpha Labs' algorithm.
Their plugin reportedly uses a trained AI model; this project uses classical
signal processing instead. Two things to know going in:

- **Closed-loop bias.** Because the Reference signal (your main mix) already
  contains each choir mic's own contribution, the adaptive filter can be
  biased toward partially cancelling the wanted direct vocal sound, not
  just the room's feedback path — this is a known, studied limitation of
  reference-based AFC in exactly this closed-loop configuration, not a bug.
  The mitigation built in here is keeping the adaptation step size small
  (the room path is fixed; singing isn't, so a slow filter locks onto the
  persistent room response rather than the moment-to-moment voice). If you
  hear the choir's direct sound thinning out or "ducking," lower
  **AFC Adaptation Speed** first. If it's still audible after that, the next
  step up in sophistication is a prediction-error-method (PEM) based AFC,
  which is a bigger undertaking than this starting point.
- Treat every constant in the DSP files (thresholds, Q, decay rates,
  oversubtraction factor, AFC step size/leakage) as a starting point — all
  of it needs tuning by ear against your actual room and choir.

One thing I could not verify without a compiler here (this environment has
no network access, so it can't fetch JUCE or build): the exact gain-scaling
convention of `juce::dsp::FFT`'s inverse transform. The code assumes it
auto-normalises by `1/N`. If the first build sounds far too loud/quiet with
all three effects at minimum (should be near-transparent), that constant is
the first thing to check — see the `NOTE` comment in `SpectralProcessor.cpp`.

## Build via GitHub Actions (no local install needed)

A workflow is included at `.github/workflows/build.yml` that builds the
plugin on a Windows runner and hands you back the `.vst3` as a downloadable
artifact — no Visual Studio, CMake, or JUCE install on your own machine.

1. Create a new **public** repo on GitHub (public repos get unlimited free
   Actions minutes; a private repo works too, using your account's free
   monthly minutes).
2. Push this whole folder to it:
   ```
   cd FeedbackSuppressorVST
   git init
   git add .
   git commit -m "Initial commit"
   git branch -M main
   git remote add origin https://github.com/<you>/<repo-name>.git
   git push -u origin main
   ```
3. On GitHub, open the **Actions** tab — the workflow starts automatically
   on push (or click **Run workflow** to trigger it manually).
4. When it finishes (a few minutes — longer the first time while JUCE
   downloads), open the completed run and scroll to **Artifacts**. Download
   `FeedbackSuppressor-VST3.zip`, unzip it, and copy the `.vst3` folder
   inside to `C:\Program Files\Common Files\VST3\`.

Any time you push a change to the DSP or the plugin, the workflow rebuilds
and gives you a fresh artifact — no local toolchain required at all.

## Build locally

If you'd rather build on your own machine, or want to debug/step through
the code:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The built VST3/AU/Standalone will be copied to your system plugin folders
automatically (`COPY_PLUGIN_AFTER_BUILD TRUE` in CMakeLists.txt).

If you already have a local JUCE checkout, edit `CMakeLists.txt`: comment
out the `FetchContent` block and use `add_subdirectory(/path/to/JUCE JUCE)`
instead.

## Project layout

```
CMakeLists.txt
Source/
  PluginProcessor.h/.cpp   — parameters, per-channel DSP chain, processBlock, bus layout
  PluginEditor.h/.cpp      — knob GUI + reference-connection status
  DSP/
    AdaptiveFeedbackCanceller.h/.cpp  — NLMS reference-based cancellation
    FeedbackSuppressorDSP.h/.cpp      — adaptive notch safety net
    SpectralProcessor.h/.cpp          — STFT denoise + dereverb
```

## Parameters

- **Adaptive Cancellation** (on/off) — needs the Reference bus connected
- **AFC Filter Length** — 256/512/1024/2048 taps (longer = models a longer
  acoustic path, e.g. a bigger room, but costs more CPU and takes longer to
  converge; re-preparing this resets the learned filter)
- **AFC Adaptation Speed** — the NLMS step size (μ); smaller = slower to
  converge but less prone to the closed-loop bias described above
- **AFC Leakage** — tiny per-sample decay on the adaptive filter's
  coefficients, guards against slow drift/instability over long sessions
- **Notch Safety Net** (on/off)
- **Feedback Threshold** (dB above local spectral neighbourhood to flag a candidate)
- **Feedback Sensitivity** (how fast it locates & releases notches)
- **Denoise** (0–1, spectral subtraction amount)
- **De-reverb** (0–1, tail subtraction amount)
- **Output Gain** (dB, makeup gain)

## Suggested next steps

- Log the AFC filter's converged coefficients/error energy over a real
  rehearsal to check it's actually converging on the room path and not
  drifting — a rising error energy over time usually means the step size is
  too high for your reference/mic correlation.
- Log/plot the notch-detector's candidate scores against real feedback
  recordings to tune `threshold`/`span`/`persistFramesNeeded`.
- Add a short crossfade when a notch filter activates/releases to avoid
  zipper noise (currently instantaneous).
- Consider a lookahead limiter after the output gain stage for live use.
- If bias from the closed-loop reference becomes audible, look into
  prediction-error-method (PEM) AFC or decorrelating the loop (e.g. a small
  frequency shift on the main output) as documented in the public-address
  feedback-cancellation literature.
