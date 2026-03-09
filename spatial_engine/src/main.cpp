// spatialroot Spatial Renderer for AlloSphere
// 
// renders spatial audio using Vector Base Amplitude Panning (VBAP),
// Distance-Based Amplitude Panning (DBAP), or Layer-Based Amplitude Panning (LBAP)
// takes mono source files and spatial trajectory data
// outputs multichannel WAV for the AlloSphere's speaker array
//
// key gotcha that took forever to debug:
// AlloLib expects speaker angles in degrees not radians
// so the JSON loader converts from radians to degrees when creating al::Speaker objects
// without this conversion panners silently fail and produce zero output
//
// DBAP coordinate quirk: AlloLib's DBAP internally swaps: Vec3d(pos.x, -pos.z, pos.y)
// We compensate by transforming (x,y,z) -> (x,z,-y) before passing to DBAP.
// See SpatialRenderer::directionToDBAPPosition() for details.

#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>

#include "JSONLoader.hpp"
#include "LayoutLoader.hpp"
#include "SpatialRenderer.hpp"
#include "WavUtils.hpp"

namespace fs = std::filesystem;

void printUsage() {
    std::cout << "spatialroot Spatial Renderer\n\n";
    std::cout << "Usage:\n"
              << "  spatialroot_spatial_render \\\n"
              << "    --layout layout.json \\\n"
              << "    --positions spatial.json \\\n"
              << "    --sources <folder> \\\n"
              << "    --out output.wav \\\n"
              << "    [OPTIONS]\n\n";
    std::cout << "Required:\n"
              << "  --layout FILE       Speaker layout JSON file\n"
              << "  --positions FILE    Spatial trajectory JSON file\n"
              << "  --out FILE          Output multichannel WAV file\n\n";
    std::cout << "Source input (one of the following is required):\n"
              << "  --sources FOLDER    Folder containing mono source WAV files\n"
              << "  --adm FILE          Multichannel ADM WAV file (direct streaming,\n"
              << "                      skips stem splitting)\n\n";
    std::cout << "Spatializer Options:\n"
              << "  --spatializer TYPE    Spatializer: vbap, dbap, or lbap (default: dbap)\n"
              << "  --dbap_focus FLOAT    DBAP focus/rolloff exponent (default: 1.0, range: 0.2-5.0)\n"
              << "  --lbap_dispersion F   LBAP dispersion threshold (default: 0.5, range: 0.0-1.0)\n\n";
    std::cout << "General Options:\n"
              << "  --master_gain FLOAT   Master gain (default: 0.25 for headroom)\n"
              << "  --solo_source NAME    Render only the named source (for debugging)\n"
              << "  --t0 SECONDS          Start time in seconds (default: 0)\n"
              << "  --t1 SECONDS          End time in seconds (default: full duration)\n"
              << "  --render_resolution MODE  Render resolution: block or sample (default: block)\n"
              << "  --block_size N        Block size in samples (default: 64, use 256 for faster renders)\n"
              << "  --vertical-compensation [fullsphere]  Vertical compensation mode (default: enabled, AtmosUp)\n"
              << "  --force_2d            Force 2D mode (flatten all elevations)\n"
              << "  --debug_dir DIR       Output debug diagnostics to directory\n"
              << "  --help                Show this help message\n\n";
    std::cout << "Spatializers:\n"
              << "  dbap   - Distance-Based Amplitude Panning (DEFAULT)\n"
              << "           Works with any speaker layout, no coverage gaps\n"
              << "           --dbap_focus controls distance attenuation (higher = sharper focus)\n"
              << "  vbap   - Vector Base Amplitude Panning\n"
              << "           Best for layouts with good 3D coverage, uses speaker triplets\n"
              << "           May have coverage gaps at zenith/nadir\n"
              << "  lbap   - Layer-Based Amplitude Panning\n"
              << "           Designed for multi-ring/layer layouts (e.g., 3 elevation rings)\n"
              << "           --lbap_dispersion controls zenith/nadir signal spread\n\n";
    // DEV NOTE: Future --spatializer auto mode could detect layout type:
    // - Single ring (2D): use DBAP
    // - Multi-ring with good coverage: use LBAP or DBAP
    // - Dense 3D coverage: use VBAP
    // For now, default to DBAP as safest option.
    std::cout << "Render Resolutions:\n"
              << "  block  - Direction computed at block center (RECOMMENDED)\n"
              << "           Use small blockSize (32-64) for smooth motion\n"
              << "  sample - Direction computed per sample (very slow, debugging only)\n"
              << "  smooth - DEPRECATED: may cause artifacts, use 'block' instead\n\n";
    std::cout << "Elevation Modes:\n"
              << "  (default) vertical compensation ON (RescaleAtmosUp):\n"
              << "             Assumes source elevations live in [0,+pi/2] (ear->top) and\n"
              << "             remaps into the layout's elevation range. Recommended for\n"
              << "             Atmos-style content.\n"
              << "  --vertical-compensation fullsphere :\n"
              << "             Rescale assuming source elevations in [-pi/2,+pi/2] (full sphere).\n"
              << "  --no-vertical-compensation :\n"
              << "             Disable vertical compensation (Clamp). Preserves input elevation\n"
              << "             but clips to layout bounds for safety.\n";
}

int main(int argc, char *argv[]) {

    // parse command line args
    // old version used positional args which was error prone
    // switched to flagged args for clarity
    if (argc < 2) {
        printUsage();
        return 1;
    }

    fs::path layoutFile, positionsFile, sourcesFolder, admFile, outFile;
    RenderConfig config;  // Uses sensible defaults: masterGain=0.25f, pannerType=DBAP, etc.

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else if (arg == "--layout") {
            layoutFile = argv[++i];
        } else if (arg == "--positions") {
            positionsFile = argv[++i];
        } else if (arg == "--sources") {
            sourcesFolder = argv[++i];
        } else if (arg == "--adm") {
            admFile = argv[++i];
        } else if (arg == "--out") {
            outFile = argv[++i];
        } else if (arg == "--spatializer") {
            std::string panner = argv[++i];
            if (panner == "dbap") {
                config.pannerType = PannerType::DBAP;
            } else if (panner == "vbap") {
                config.pannerType = PannerType::VBAP;
            } else if (panner == "lbap") {
                config.pannerType = PannerType::LBAP;
            } else {
                std::cerr << "Error: unknown spatializer '" << panner << "'\n";
                std::cerr << "Valid spatializers: vbap, dbap, lbap\n";
                return 1;
            }
        } else if (arg == "--dbap_focus") {
            config.dbapFocus = std::stof(argv[++i]);
            if (config.dbapFocus < 0.2f || config.dbapFocus > 5.0f) {
                std::cerr << "Warning: --dbap_focus " << config.dbapFocus 
                          << " is outside recommended range [0.2, 5.0]\n";
            }
        } else if (arg == "--lbap_dispersion") {
            config.lbapDispersion = std::stof(argv[++i]);
            if (config.lbapDispersion < 0.0f || config.lbapDispersion > 1.0f) {
                std::cerr << "Warning: --lbap_dispersion " << config.lbapDispersion 
                          << " is outside recommended range [0.0, 1.0]\n";
            }
        } else if (arg == "--master_gain") {
            config.masterGain = std::stof(argv[++i]);
            if (config.masterGain < 0.0f || config.masterGain > 1.0f) {
                std::cerr << "Error: --master_gain must be in range [0.0, 1.0]\n";
                return 1;
            }
        } else if (arg == "--solo_source") {
            config.soloSource = argv[++i];
        } else if (arg == "--t0") {
            config.t0 = std::stod(argv[++i]);
        } else if (arg == "--t1") {
            config.t1 = std::stod(argv[++i]);
        } else if (arg == "--debug_dir") {
            config.debugDiagnostics = true;
            config.debugOutputDir = argv[++i];
        } else if (arg == "--render_resolution") {
            std::string res = argv[++i];
            if (res == "block" || res == "smooth" || res == "sample") {
                config.renderResolution = res;
            } else {
                std::cerr << "Error: unknown render resolution '" << res << "'\n";
                std::cerr << "Valid resolutions: block, smooth, sample\n";
                return 1;
            }
        } else if (arg == "--block_size") {
            config.blockSize = std::stoi(argv[++i]);
            if (config.blockSize < 1 || config.blockSize > 8192) {
                std::cerr << "Error: block_size must be between 1 and 8192\n";
                return 1;
            }
        } else if (arg == "--elevation_mode") {
            // Backwards-compatible: accept old flag values
            std::string mode = argv[++i];
            if (mode == "compress") {
                config.elevationMode = ElevationMode::RescaleFullSphere; // map old 'compress' to full-sphere rescale
            } else if (mode == "clamp") {
                config.elevationMode = ElevationMode::Clamp;
            } else {
                std::cerr << "Error: unknown elevation mode '" << mode << "'\n";
                std::cerr << "Valid modes: compress, clamp\n";
                return 1;
            }
        } else if (arg == "--no-vertical-compensation") {
            config.elevationMode = ElevationMode::Clamp;
        } else if (arg == "--vertical-compensation") {
            // Optional argument: 'fullsphere' selects full-sphere mapping.
            // If no argument provided (flag alone), keep default RescaleAtmosUp.
            if (i + 1 < argc) {
                std::string val = argv[i+1];
                if (val == "fullsphere") {
                    config.elevationMode = ElevationMode::RescaleFullSphere;
                    i++; // consume argument
                }
            }
        } else if (arg == "--force_2d") {
            config.force2D = true;
        }
    }

    // Validate required arguments
    if (layoutFile.empty()) {
        std::cerr << "Error: --layout is required\n";
        return 1;
    }
    if (positionsFile.empty()) {
        std::cerr << "Error: --positions is required\n";
        return 1;
    }
    if (outFile.empty()) {
        std::cerr << "Error: --out is required\n";
        return 1;
    }
    if (sourcesFolder.empty() && admFile.empty()) {
        std::cerr << "Error: Either --sources or --adm is required\n";
        return 1;
    }
    if (!sourcesFolder.empty() && !admFile.empty()) {
        std::cerr << "Error: --sources and --adm are mutually exclusive\n";
        return 1;
    }

    bool useADM = !admFile.empty();

    // layout JSON has speaker positions in radians
    // these get converted to degrees when creating al::Speaker objects in SpatialRenderer
    std::cout << "Loading layout...\n";
    SpeakerLayoutData layout = LayoutLoader::loadLayout(layoutFile);

    // spatial trajectories from LUSID scene (frames/nodes format)
    std::cout << "Loading LUSID scene...\n";
    SpatialData spatial = JSONLoader::loadLusidScene(positionsFile);

    // load all mono source files
    std::cout << "Loading source WAVs...\n";
    std::map<std::string, MonoWavData> sources;
    if (useADM) {
        std::cout << "  ADM file: " << admFile << " (direct channel extraction)\n";
        sources = WavUtils::loadSourcesFromADM(admFile, spatial.sources, spatial.sampleRate);
    } else {
        sources = WavUtils::loadSources(sourcesFolder, spatial.sources, spatial.sampleRate);
    }

    // main rendering happens here
    // this is where the degrees conversion and channel mapping fixes are critical
    std::cout << "Rendering...\n";
    SpatialRenderer renderer(layout, spatial, sources);
    MultiWavData output = renderer.render(config);

    // output has consecutive channels 0 to numSpeakers
    // if you need AlloSphere hardware channel numbers with gaps you can remap later
    std::cout << "Writing output WAV: " << outFile << "\n";
    WavUtils::writeMultichannelWav(outFile, output);

    std::cout << "Done.\n";
    return 0;
}
