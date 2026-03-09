# Offline Rendering Pipeline Documentation

**Last Updated:** March 9, 2026  
**Version:** 3.9 (Modernized)

## Overview

The offline rendering pipeline provides batch processing capabilities for spatial audio rendering, now fully modernized to match the realtime engine's architecture and performance. It supports both ADM direct input (recommended) and LUSID package input (legacy compatibility).

## Architecture

The pipeline consists of three main components:

1. **ADM Preprocessing** - `cult-transcoder` for ADM→LUSID conversion
2. **Spatial Rendering** - C++ spatial renderer with dual input support
3. **Analysis** - Optional PDF analysis of rendered output

## Input Modes

### ADM Direct Input (Recommended)

**Workflow:**

```
ADM WAV → cult-transcoder transcode → scene.lusid.json
    ↓
spatialroot_spatial_render --adm <file> --positions <json> --layout <json> --out <wav>
```

**Advantages:**

- No intermediate file creation
- Direct multichannel streaming
- Fastest processing
- Matches realtime pipeline workflow

**Usage:**

```bash
# Basic usage (uses defaults)
python runPipeline.py sourceData/driveExampleSpruce.wav

# Full parameter control
python runPipeline.py sourceData/driveExampleSpruce.wav spatial_engine/speaker_layouts/allosphere_layout.json dbap 1.5 0.5 true
```

### LUSID Package Input (Legacy)

**Workflow:**

```
LUSID Package (scene.lusid.json + mono WAVs) → validation
    ↓
spatialroot_spatial_render --sources <folder> --positions <json> --layout <json> --out <wav>
```

**Advantages:**

- Backward compatibility
- Pre-processed mono stems
- Existing workflow preservation

**Usage:**

```bash
# Basic usage (uses defaults)
python runPipeline.py sourceData/lusid_package

# Full parameter control
python runPipeline.py sourceData/lusid_package spatial_engine/speaker_layouts/allosphere_layout.json dbap 1.5 0.5 true
```

## Command Line Interface

### runPipeline.py

```bash
python runPipeline.py [input] [speaker_layout] [spatializer] [resolution] [master_gain] [analysis]
```

**Arguments:**

- `input` - ADM WAV file or LUSID package directory (auto-detected)
- `speaker_layout` - Speaker layout JSON file (default: allosphere_layout.json)
- `spatializer` - Spatialization algorithm: dbap, vbap, lbap (default: dbap)
- `resolution` - Spatial resolution parameter (default: 1.5)
- `master_gain` - Master gain in dB (default: 0.5)
- `analysis` - Generate PDF analysis: true/false (default: true)

### spatialroot_spatial_render

```bash
spatialroot_spatial_render --adm <adm_file> --positions <scene.json> --layout <layout.json> --out <output.wav> [spatializer_options]
# OR
spatialroot_spatial_render --sources <package_dir> --positions <scene.json> --layout <layout.json> --out <output.wav> [spatializer_options]
```

**Arguments:**

- `--adm <file>` - Direct ADM WAV input (mutually exclusive with --sources)
- `--sources <dir>` - LUSID package directory (mutually exclusive with --adm)
- `--positions <file>` - LUSID scene JSON (required)
- `--layout <file>` - Speaker layout JSON (required)
- `--out <file>` - Output WAV file (required)

**Spatializer Options:**

- `--spatializer <dbap|vbap|lbap>` - Spatialization algorithm (default: dbap)
- `--vbap-triangulation <file>` - VBAP triangulation file
- `--dbap-rolloff <float>` - DBAP rolloff factor (default: 6.0)
- `--lbap-mode <int>` - LBAP mode (0-2)

## Spatializers

| Spatializer | Coverage      | Layout Requirements   | Best For                  |
| ----------- | ------------- | --------------------- | ------------------------- |
| **DBAP**    | No gaps       | Any layout            | Unknown/irregular layouts |
| **VBAP**    | Can have gaps | Good 3D triangulation | Dense 3D arrays           |
| **LBAP**    | No gaps       | Multi-ring layers     | Allosphere, TransLAB      |

## Output Analysis

When analysis is enabled (default), the pipeline generates a comprehensive PDF report including:

- Peak and RMS levels for each output channel
- Spatial distribution analysis
- Frequency response plots
- Channel correlation matrix

## Dependencies

- **cult-transcoder** - ADM preprocessing (built automatically)
- **spatialroot_spatial_render** - C++ spatial renderer (built automatically)
- **Python packages** - numpy, matplotlib, PyPDF2, etc.

## Error Handling

The pipeline provides clear error messages for common issues:

- Missing input files
- Malformed ADM/LUSID data
- cult-transcoder failures
- Spatial renderer errors
- Layout/speaker configuration issues

## Performance

**ADM Direct Input:**

- ~110 seconds for typical Atmos master
- 56-channel output, ~1.1 GB
- Memory efficient (no intermediate files)

**LUSID Package Input:**

- ~98 seconds for mono stem packages
- 56-channel output, ~1.0 GB
- Requires pre-split audio stems

## Backward Compatibility

The modernized pipeline maintains full backward compatibility with existing LUSID package workflows while adding the new ADM direct input capability.

## Troubleshooting

### Common Issues

**"cult-transcoder not found"**

- Ensure `init.sh` completed successfully
- Check `cult_transcoder/build/cult-transcoder` exists

**"ADM file format error"**

- Verify ADM WAV is valid BW64 format
- Check ADM metadata is present

**"Spatial renderer failed"**

- Ensure speaker layout JSON is valid
- Check LUSID scene positions are within layout bounds

### Debug Mode

Enable verbose logging (runPipeline.py doesn't have built-in verbose flags, but you can check the output for detailed progress messages):

```bash
python runPipeline.py sourceData/lusid_package spatial_engine/speaker_layouts/allosphere_layout.json dbap 1.5 0.5 true
```

## Future Development

- Unified realtime/offline renderer binary
- OSC parameter automation during render
- GPU acceleration for faster rendering
- Streaming output for large renders
