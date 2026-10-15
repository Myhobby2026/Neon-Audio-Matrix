# Neon Audio Matrix (Windows 10/11, Visual Studio 2026)

A JUCE/CMake desktop host plus a separate DSP DLL. The host loads `NeonEngine.dll` at runtime with `juce::DynamicLibrary`; the engine owns a ten-node `juce::AudioProcessorGraph` and exports only a small C ABI. No JUCE or C++ object crosses the DLL boundary.

## Visual Studio 2026 support

This project is configured specifically for the **Visual Studio 18 2026** CMake generator and x64 MSVC builds. Use Visual Studio 2026 with:

- **Desktop development with C++** and the VS 2026 MSVC toolset
- A Windows 10/11 SDK
- CMake **4.2 or newer** (VS 2026's CMake integration must include the `Visual Studio 18 2026` generator)
- Git and network access for the first configure; CMake fetches JUCE 8.0.15

### Open/build in Visual Studio

1. Extract/open the `NeonAudioMatrix` folder.
2. In Visual Studio 2026, choose **File → Open → Folder** and select this folder.
3. Select the **vs2026-x64** configure preset, then choose the `NeonHost` startup target and `Release` or `Debug`.
4. Build. The post-build step copies `NeonEngine.dll` beside the host executable.

Or use the Visual Studio Developer PowerShell / terminal from this directory:

```powershell
cmake --preset vs2026-x64
cmake --build --preset vs2026-release
```

The generated VS solution/build files are placed in `build/vs2026-x64`. They are generated locally and are not checked into this source tree.

## Audio setup and safety

- The host registers JUCE's **shared-mode WASAPI** device type (and no DirectSound/ASIO alternative), so capture and playback stay on WASAPI shared mode. The callback uses the active device's sample rate and block size.
- **WASAPI loopback** is implemented with Windows Core Audio (`IAudioClient` + `IAudioCaptureClient`) in a capture worker. The worker feeds a bounded lock-free stereo FIFO; the low-latency JUCE output callback processes the exact frame count supplied by the selected output device. Oversized callbacks are chunked without changing their total sample count.
- The loopback source is Windows' current default render endpoint. The default startup capture mode is loopback.
- If VB-Audio Virtual Cable is installed, startup searches for **`CABLE Output`** and selects it as the JUCE/WASAPI input device. In the **DEVICES** tab, choose **WASAPI INPUT** to use that capture endpoint. You can also select it manually in the embedded audio-device selector. This project does not install or redistribute VB-CABLE.
- **Feedback protection:** if the loopback source and active output appear to be the same endpoint, the host mutes its processed output. Choose a different playback device in the DEVICES tab to hear processed loopback audio. For VB-CABLE routing, use `CABLE Output` as the capture endpoint and a separate playback device for the output.
- For best clock/latency behaviour, use matching sample rates for the loopback source and output device. Shared-mode WASAPI latency depends on the Windows endpoint and device settings; this is not an exclusive-mode or ASIO driver.

## Graph and processor nodes

The graph contains exactly ten AudioProcessor nodes, in this order:

1. Input (`InputNode`, JUCE graph I/O processor wrapper)
2. EQ (`EqNode`, ten logarithmically spaced graphic bands with shared Q control)
3. Spectrum Analyzer (`SpectrumAnalyzerNode`, pass-through FFT tap with 64 atomic bands)
4. Reverb (`ReverbNode`, multiband feedback-comb reverb with wet/dry mix and bypass)
5. Field Surround (`FieldSurroundNode`, bypassable virtual field with independent field, spread, bass, and center controls)
6. Stereo Delay (`StereoDelayNode`, bypassable cross-feedback stereo delay)
7. Dimension (`DimensionNode`, bypassable modulated short-delay widening)
8. Utility (`UtilityNode`, switchable DC blocker and bypassable bass/treble Loudness contour)
9. Master Volume (`MasterVolumeNode`, smoothed gain)
10. Output (`OutputNode`, JUCE graph I/O processor wrapper)

All processing nodes declare stereo input/output buses in their `AudioProcessor` constructors. The graph is connected by channel using `AudioProcessorGraph::addConnection()`; connections are constructed synchronously before playback.

**JUCE does not provide a `setInputOutputBus` graph-connection method.** Bus layouts are declared with `BusesProperties` / `setBusesLayout`; graph pins are wired with `addConnection({{sourceNode, sourceChannel}, {destinationNode, destinationChannel}})`. The two I/O wrappers derive from JUCE's `AudioGraphIOProcessor`, which itself is an `AudioProcessor` and represents the graph's external input/output.

## Render & Export

The **RENDER** tab provides a file queue with Add/Clear, preview play/pause, stop, previous/next, fast-forward, sequential queue playback, a seek bar, and an FX on/off switch. It imports WAV, MP3, FLAC, OGG, and AIFF. Choose **Stereo**, **Virtual Surround (headphones)**, or **5.1 upmix (speakers)**. Virtual Surround applies a crossfeed and delayed-side stereo effect intended for headphones and still renders two channels. The 5.1 option synthetically derives front left/right, center, low-frequency-effects (LFE), and left/right surround channels from the processed stereo signal, then writes a six-channel WAV or FLAC using the named 5.1 speaker layout. This is an upmix, not recovery of genuine discrete surround: a stereo MP3 does not contain the original six channels. MP3 remains stereo-only and is disabled while 5.1 is selected. WAV/FLAC export supports 44.1, 48, or 96 kHz and 16- or 24-bit depth; MP3 export uses the Windows Media Foundation encoder at 44.1 or 48 kHz with selectable 128, 192, 256, or 320 kbps. A pasted YouTube link can open its official page in the default browser only; the app does not download or extract YouTube audio. Batch rendering runs offline with progress/status reporting, a completion popup, an output-folder chooser, and an Open Folder action. Optional K-weighted integrated loudness normalization targets approximately **-14 LUFS** while limiting peaks across the output channels to **-1 dBFS**.

## Presets and DLL ABI

- `AudioEngine` stores parameter values in atomics shared by the graph processors.
- Presets are represented by a JUCE `ValueTree` (`NEON_PRESET`) and converted to/from readable JSON using JUCE's JSON API. Schema 6 stores the advanced reverb controls, Stereo Delay crossovers, Loudness controls, and Field Surround controls. Schemas 1–5 remain loadable with sensible defaults; old `surround_width` values migrate to the new Spread scale. Newer schema numbers are accepted and unknown properties are ignored. Use the **PRESETS** tab to save/load `.json` files.
- The host resolves `neon_create_engine`, `neon_prepare_engine`, `neon_process_engine`, parameter/spectrum functions, and preset functions from the DLL. The C ABI is versioned (ABI 6 adds Field Surround's Field, Bass, and Center parameters while retaining the legacy width ID as Spread); the host rejects a mismatched ABI.
- The header's **LISTEN ORIGINAL** toggle switches between the raw captured input and processed output with a short crossfade; the engine keeps running so effect state stays warm. The **EQUALISER** view includes a 10-band graphic EQ, preamp, band Q, quick curves, live spectrum, limiter control, and input/output meters. **SURROUND** contains the Field Surround panel with a live surround-field visualization, Field, Spread, Bass, and Center controls, plus an oversized Master Volume knob. Its non-master knobs and all **OTHER SPATIAL EFFECTS** knobs match the Reverb knob size. **OTHER SPATIAL EFFECTS**, placed immediately after **REVERB**, contains Dimension, Stereo Delay, Loudness, DC Block, and their independent bypass switches. Stereo Delay's low/high crossovers shape its repeat tone; Loudness applies a broad bass/treble contour with a separate amount control and bypass. **REVERB** has its own synchronized bypass switch and advanced reverb controls. Quick EQ curves are separate from the readable JSON preset system.
- `NeonEngine.dll` is a custom processor library, **not a VST3 plug-in**. It is intended to be loaded by the included host through `DynamicLibrary`.

## Project layout

```text
CMakeLists.txt
CMakePresets.json
include/Neon/EngineApi.h
src/engine/AudioEngine.{h,cpp}
src/engine/Nodes.{h,cpp}
src/engine/EngineApi.cpp
src/host/EngineLibrary.{h,cpp}
src/host/HostAudioCallback.{h,cpp}
src/host/WasapiLoopback.{h,cpp}
src/host/NeonLookAndFeel.{h,cpp}
src/host/MainComponent.{h,cpp}
src/host/RenderExportPage.{h,cpp}
src/host/Main.cpp
resources/NeonAudioMatrix.ico
resources/NeonAudioMatrixIcon.png
resources/NeonAudioMatrix.rc.in
```

The UI has **EQUALISER**, **SURROUND**, **REVERB**, **OTHER SPATIAL EFFECTS**, **GRAPH**, **DEVICES**, **PRESETS**, and **RENDER** tabs with a dark neon LookAndFeel, rounded controls, a live response/spectrum display, input/output meters, a graph-routing diagram, WASAPI device selection, JSON preset controls, and the Render & Export file queue. The dedicated **REVERB** tab exposes wet/dry mix (0% dry to 100% wet), main decay time, low/high crossover frequencies and decay multipliers, pre-delay, low/high damping, stereo width, room size/aspect ratio, and diffusion. A matching Windows `.ico` resource and custom JUCE title-bar icon are included.

## Licensing

JUCE is fetched from its upstream repository. Review JUCE's current licence and choose the AGPLv3 or a commercial JUCE licence that fits your distribution before shipping a product. Windows and VB-CABLE system components remain subject to their respective licenses.
