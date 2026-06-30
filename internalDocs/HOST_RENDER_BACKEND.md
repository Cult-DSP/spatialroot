# Host Render Backend (Internal Host Bus)

## Summary

This document captures the current host-render backend implementation that allows an external host to pull interleaved PCM blocks from Spatial Root without opening a hardware audio device.

The implementation is intentionally minimal and additive. The default output mode remains device-owned playback via AlloLib `AudioIO`.

## Current Realtime Audio Path

### Audio Callback Entry Point

- Callback registration: `RealtimeBackend::init()` in `source/spatial_engine/realtimeEngine/src/RealtimeBackend.hpp`
- Callback entry: `RealtimeBackend::audioCallback(al::AudioIOData& io)`
- Per-block render path: `RealtimeBackend::processBlock(al::AudioIOData& io)`

### Internal Render Bus

- Internal bus: `Spatializer::mRenderIO`
- Bus width: `Spatializer::numInternalChannels()`
- Internal bus allocation: `Spatializer::init()` allocates `mRenderIO` using `mConfig.bufferSize`

### Output Bus / Device Routing

- Physical output bus width: `RealtimeConfig::outputChannels` (derived from the layout's maximum resolved device channel)
- Routing: `Spatializer::renderBlock()` Phase 7 (identity copy or scatter via `OutputRemap`)

## New Host Render API

### Types

- `AudioOutputMode` selects hardware device vs internal host bus
- `HostBusConfig` defines host block size, sample rate, output channel count, and interleaving mode

### EngineSession Additions

```cpp
bool setAudioOutputMode(AudioOutputMode mode);
bool prepareInternalHostBus(const HostBusConfig& config);
int renderHostBlock(float* interleavedOutput, int numFrames, int numChannels);
void shutdownInternalHostBus();
int getRequiredOutputChannelCount() const;
std::string getLastWarning() const;
```

### RealtimeBackend Additions

- `prepareInternalHostBus(const HostBusConfig& config)` configures a host IO buffer without opening a device
- `renderHostBlock()` runs the per-block render pipeline into the internal bus
- `shutdownInternalHostBus()` clears host-bus prepared state

## Expected Host Flow

```text
EngineSession session;

session.configureEngine(options);
session.loadScene(scene);
session.applyLayout(layout);
session.configureRuntime(params);

session.setAudioOutputMode(AudioOutputMode::InternalHostBus);
session.prepareInternalHostBus({48000.0, 512, hostChannels, true});

while (hostAudioIsRunning)
{
    session.renderHostBlock(hostBuffer, 512, hostChannels);
}

session.shutdownInternalHostBus();
session.shutdown();
```

## Channel Mismatch Behavior

`EngineSession::renderHostBlock()` handles channel mismatches conservatively:

- Host channels fewer than required layout channels: render only the first `hostChannels`, then store a warning in `getLastWarning()`
- Host channels greater than required layout channels: render the required channels and zero-fill the extra host channels, then store a warning

## Constraints and Notes

- The host must match `sampleRate` and `blockSize` with the configured `EngineSession` values
- Only interleaved output is supported in this implementation
- `renderHostBlock()` zero-fills the host buffer on error
- Hardware output and Internal Host Bus cannot run simultaneously

## Pending Validation

A dedicated host-render smoke test is still recommended: load known content, render several blocks, verify nonzero and NaN-free output, and optionally dump a WAV. Add this once a stable test input/layout pair is selected.
