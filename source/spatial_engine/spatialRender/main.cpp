// spatialroot Spatial Renderer for AlloSphere
//
// renders spatial audio using Distance-Based Amplitude Panning (DBAP)
// or Layer-Based Amplitude Panning (LBAP)
// takes mono source files or multichannel ADM WAV and spatial trajectory data
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
#include <sstream>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <ctime>

#ifndef _WIN32
#  include <sys/wait.h>
#endif

#include "SpatialRenderer.hpp"
#include "OfflineOutputRouteMap.hpp"
#include "../src/JSONLoader.hpp"
#include "../src/LayoutLoader.hpp"
#include "../src/WavUtils.hpp"

namespace fs = std::filesystem;

// ── Subprocess helper ─────────────────────────────────────────────────────────
// Builds a shell-quoted command string and invokes it via std::system().
// stdout/stderr are inherited (not captured). Each argument is double-quoted;
// internal double-quotes are backslash-escaped so paths with spaces work.
static int runProcessSync(const std::vector<std::string>& args) {
    std::ostringstream cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmd << ' ';
        cmd << '"';
        for (char c : args[i]) {
            if (c == '"') cmd << '\\';
            cmd << c;
        }
        cmd << '"';
    }
    int ret = std::system(cmd.str().c_str());
#ifndef _WIN32
    if (WIFEXITED(ret))        ret = WEXITSTATUS(ret);
    else if (WIFSIGNALED(ret)) ret = 128 + WTERMSIG(ret);
#endif
    return ret;
}

// ── CULT binary resolution ────────────────────────────────────────────────────
// Resolution order:
//   1. --cult-transcoder CLI override
//   2. CULT_TRANSCODER environment variable
//   3. build/internal/cult_transcoder/ relative to cwd
//   4. internal/cult_transcoder/build/ relative to cwd
//   5. Executable-relative equivalents (handles running from build output dir)
static fs::path resolveCultBinary(const fs::path& cliOverride,
                                  const fs::path& executablePath) {
#ifdef _WIN32
    const std::string baseName = "cult-transcoder.exe";
#else
    const std::string baseName = "cult-transcoder";
#endif

    std::vector<fs::path> candidates;

    if (!cliOverride.empty())
        candidates.push_back(cliOverride);

    const char* cultEnv = std::getenv("CULT_TRANSCODER");
    if (cultEnv && *cultEnv != '\0')
        candidates.push_back(fs::path(cultEnv));

    // Candidates relative to cwd (repo root convention)
    candidates.push_back(fs::path("build/internal/cult_transcoder") / baseName);
    candidates.push_back(fs::path("internal/cult_transcoder/build") / baseName);

    // Candidates relative to the executable directory.
    // The offline renderer lives at build/source/spatial_engine/spatialRender/.
    // Going up 3 directories from there reaches build/; going up 4 reaches repo root.
    if (!executablePath.empty()) {
        fs::path exeDir = executablePath.parent_path();
        candidates.push_back((exeDir / "../../../internal/cult_transcoder" / baseName).lexically_normal());
        candidates.push_back((exeDir / "../../../../build/internal/cult_transcoder" / baseName).lexically_normal());
        candidates.push_back((exeDir / "../../../../internal/cult_transcoder/build" / baseName).lexically_normal());
    }

    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec) && !ec)
            return c;
    }

    std::cerr << "Error: cult-transcoder binary not found.\n";
    std::cerr << "Searched:\n";
    for (const auto& c : candidates)
        std::cerr << "  " << c.string() << "\n";
    std::cerr << "Use --cult-transcoder PATH or set the CULT_TRANSCODER environment variable.\n";
    return std::filesystem::path();
}

// ── Offline temp directory ────────────────────────────────────────────────────
// Creates a uniquely named temp directory with a reports/ subdirectory.
// Returns an empty path on failure.
static std::filesystem::path makeOfflineTempDir() {
    namespace fs = std::filesystem;
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    std::ostringstream name;
    name << "spatialroot-offline-" << std::time(nullptr) << "-" << (std::rand() % 100000);
    fs::path dir = fs::temp_directory_path() / name.str();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return std::filesystem::path();
    fs::create_directories(dir / "reports", ec);
    if (ec) return std::filesystem::path();
    return dir;
}

void printUsage() {
    std::cout << "spatialroot Spatial Renderer\n\n";
    std::cout << "Usage:\n"
              << "  spatialroot_spatial_render \\\n"
              << "    --layout layout.json \\\n"
              << "    --adm source.wav \\\n"
              << "    --out output.wav \\\n"
              << "    [OPTIONS]\n\n";
    std::cout << "Required:\n"
              << "  --layout FILE       Speaker layout JSON file\n"
              << "  --out FILE          Output multichannel WAV file\n\n";
    std::cout << "Source input (one required, mutually exclusive):\n"
              << "  --sources FOLDER    Folder containing mono source WAV files\n"
              << "  --adm FILE          Multichannel ADM WAV file (direct channel extraction)\n"
              << "                      Without --positions: invokes CULT Transcoder automatically\n"
              << "                      to generate scene.lusid.json from the ADM metadata.\n"
              << "                      With --positions: uses the provided scene file directly.\n\n";
    std::cout << "Scene input:\n"
              << "  --positions FILE    LUSID spatial scene JSON file\n"
              << "                      Required when using --sources.\n"
              << "                      Optional when using --adm (auto-generated by CULT if omitted).\n\n";
    std::cout << "ADM / CULT Options:\n"
              << "  --cult-transcoder PATH  Explicit path to cult-transcoder binary.\n"
              << "                          Also reads CULT_TRANSCODER environment variable.\n"
              << "                          Default search order (cwd-relative then exe-relative):\n"
              << "                            build/internal/cult_transcoder/cult-transcoder\n"
              << "                            internal/cult_transcoder/build/cult-transcoder\n"
              << "  --keep-temp-dir     Preserve the temp directory created by CULT transcoding.\n"
              << "                      Default: deleted on success. Always preserved on failure.\n\n";
    std::cout << "  Offline ADM source mapping (applies when --adm is used, not a general LUSID rule):\n"
              << "    CULT generates scene.lusid.json with node IDs derived from ADM track numbers.\n"
              << "    The offline renderer maps those node IDs back to WAV channels as follows:\n"
              << "      'N.1' -> WAV channel N-1 (0-based)  e.g. '1.1'->ch0, '2.1'->ch1\n"
              << "      'LFE' -> WAV channel 3 when the file has >= 4 channels (matches --lfe-mode hardcoded)\n"
              << "    Sources that cannot be mapped using this convention are a hard failure.\n\n";
    std::cout << "Layout / routing diagnostics:\n"
              << "  --print-output-route-map  Load the layout, print offline channel routing, and exit\n"
              << "  --validate-layout-only    Alias for --print-output-route-map\n\n";
    std::cout << "Spatializer Options:\n"
              << "  --spatializer TYPE    Spatializer: dbap or lbap (default: dbap)\n"
              << "  --dbap_focus FLOAT    DBAP focus/rolloff exponent (default: 1.0, range: 0.1-5.0)\n"
              << "  --lbap_dispersion F   LBAP dispersion threshold (default: 0.5, range: 0.0-1.0)\n\n";
    std::cout << "General Options:\n"
              << "  --master_gain DB      Master gain in dB -60–+12 (default: 0, 0 dB = unity)\n"
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
              << "  lbap   - Layer-Based Amplitude Panning\n"
              << "           Designed for multi-ring/layer layouts (e.g., 3 elevation rings)\n"
              << "           --lbap_dispersion controls zenith/nadir signal spread\n\n";
    // DEV NOTE: Future --spatializer auto mode could detect layout type:
    // - Single ring (2D): use DBAP
    // - Multi-ring with good coverage: use LBAP or DBAP
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
    fs::path cultTranscoderPath;
    RenderConfig config;  // Uses sensible defaults: masterGainDb=0.0f (0 dB = unity), pannerType=DBAP, etc.
    bool printOutputRouteMap = false;
    bool keepTempDir = false;

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
        } else if (arg == "--cult-transcoder") {
            cultTranscoderPath = argv[++i];
        } else if (arg == "--keep-temp-dir") {
            keepTempDir = true;
        } else if (arg == "--spatializer") {
            std::string panner = argv[++i];
            if (panner == "dbap") {
                config.pannerType = PannerType::DBAP;
            } else if (panner == "lbap") {
                config.pannerType = PannerType::LBAP;
            } else {
                std::cerr << "Error: unknown spatializer '" << panner << "'\n";
                std::cerr << "Valid spatializers: dbap, lbap\n";
                return 1;
            }
        } else if (arg == "--dbap_focus") {
            config.dbapFocus = std::stof(argv[++i]);
            if (config.dbapFocus < 0.1f || config.dbapFocus > 5.0f) {
                std::cerr << "Warning: --dbap_focus " << config.dbapFocus
                          << " is outside recommended range [0.1, 5.0]\n";
            }
        } else if (arg == "--lbap_dispersion") {
            config.lbapDispersion = std::stof(argv[++i]);
            if (config.lbapDispersion < 0.0f || config.lbapDispersion > 1.0f) {
                std::cerr << "Warning: --lbap_dispersion " << config.lbapDispersion
                          << " is outside recommended range [0.0, 1.0]\n";
            }
        } else if (arg == "--master_gain") {
            config.masterGainDb = std::stof(argv[++i]);
            if (config.masterGainDb < -60.0f || config.masterGainDb > 12.0f) {
                std::cerr << "Warning: --master_gain " << config.masterGainDb
                          << " dB is outside supported range [-60, +12]\n";
            }
        } else if (arg == "--solo_source") {
            config.soloSource = argv[++i];
        } else if (arg == "--print-output-route-map" || arg == "--validate-layout-only") {
            printOutputRouteMap = true;
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

    if (printOutputRouteMap) {
        std::cout << "Loading layout...\n";
        SpeakerLayoutData layout = LayoutLoader::loadLayout(layoutFile.string());
        OfflineOutputRouteMap routeMap = buildOfflineOutputRouteMap(layout);
        printOfflineOutputRouteMap(std::cout, routeMap);
        return routeMap.valid() ? 0 : 2;
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

    // --positions is required with --sources; optional with --adm (CULT auto-invoked if missing)
    if (positionsFile.empty() && !sourcesFolder.empty()) {
        std::cerr << "Error: --positions is required when using --sources\n";
        return 1;
    }

    // Track temp dir created for CULT-generated scenes; empty = no temp dir in use.
    // On success: deleted unless --keep-temp-dir. On failure: preserved.
    fs::path cultTempDir;

    // --adm without --positions: invoke CULT Transcoder to generate scene.lusid.json
    if (!admFile.empty() && positionsFile.empty()) {
        fs::path cultBin = resolveCultBinary(cultTranscoderPath, fs::path(argv[0]));
        if (cultBin.empty()) return 1;

        cultTempDir = makeOfflineTempDir();
        if (cultTempDir.empty()) {
            std::cerr << "Error: failed to create offline temp directory under "
                      << fs::temp_directory_path().string() << "\n";
            return 1;
        }

        fs::path generatedScene = cultTempDir / "scene.lusid.json";
        fs::path reportFile     = cultTempDir / "reports" / "transcode_report.json";

        std::cout << "Invoking CULT Transcoder to generate LUSID scene from ADM WAV...\n";
        int cultRet = runProcessSync({
            cultBin.string(),
            "transcode",
            "--in",         admFile.string(),
            "--in-format",  "adm_wav",
            "--out",        generatedScene.string(),
            "--out-format", "lusid_json",
            "--report",     reportFile.string(),
            "--lfe-mode",   "hardcoded"
        });

        if (cultRet != 0) {
            std::cerr << "Error: CULT Transcoder failed (exit code " << cultRet << ")\n";
            std::cerr << "  ADM file:  " << admFile.string() << "\n";
            std::cerr << "  Expected:  " << generatedScene.string() << "\n";
            std::cerr << "  Temp dir preserved: " << cultTempDir.string() << "\n";
            cultTempDir.clear();  // mark preserved so end-of-function does not delete
            return 1;
        }

        std::error_code ec;
        if (!fs::exists(generatedScene, ec)) {
            std::cerr << "Error: CULT Transcoder exited 0 but scene.lusid.json was not written.\n";
            std::cerr << "  Expected:  " << generatedScene.string() << "\n";
            std::cerr << "  ADM file:  " << admFile.string() << "\n";
            std::cerr << "  Temp dir preserved: " << cultTempDir.string() << "\n";
            cultTempDir.clear();
            return 1;
        }

        positionsFile = generatedScene;
        std::cout << "CULT Transcoder complete. Scene: " << positionsFile.string() << "\n";
    }

    bool useADM = !admFile.empty();

    // layout JSON has speaker positions in radians
    // these get converted to degrees when creating al::Speaker objects in SpatialRenderer
    std::cout << "Loading layout...\n";
    SpeakerLayoutData layout = LayoutLoader::loadLayout(layoutFile.string());
    OfflineOutputRouteMap routeMap = buildOfflineOutputRouteMap(layout);
    if (!routeMap.valid()) {
        std::cerr << "Error: invalid offline output route map for layout '" << layoutFile.string() << "'\n";
        for (const auto &error : routeMap.errors) {
            std::cerr << "  " << error << "\n";
        }
        if (!cultTempDir.empty()) {
            std::cerr << "Temp dir preserved: " << cultTempDir.string() << "\n";
            cultTempDir.clear();
        }
        return 2;
    }
    if (!routeMap.warnings.empty()) {
        std::cout << "Offline output routing: " << routeMap.internalChannelCount
                  << " internal -> " << routeMap.outputChannelCount << " output channels";
        if (!routeMap.silentOutputChannels.empty()) {
            std::cout << " (" << routeMap.silentOutputChannels.size() << " silent gap channels)";
        }
        std::cout << "\n";
    }

    // Wrap render pipeline in try-catch so failures preserve the CULT temp dir
    // and print a clean error rather than an unhandled exception.
    try {
        // spatial trajectories from LUSID scene (frames/nodes format)
        std::cout << "Loading LUSID scene...\n";
        SpatialData spatial = JSONLoader::loadLusidScene(positionsFile.string());

        // load all mono source files
        std::cout << "Loading source WAVs...\n";
        std::map<std::string, MonoWavData> sources;
        if (useADM) {
            std::cout << "  ADM file: " << admFile.string() << " (direct channel extraction)\n";
            sources = WavUtils::loadSourcesFromADM(admFile.string(), spatial.sources, spatial.sampleRate);

            // Verify every source declared in the LUSID scene was successfully loaded.
            // WavUtils::loadSourcesFromADM silently skips sources whose node IDs cannot be
            // mapped using the CULT convention; catch that here and fail clearly.
            std::vector<std::string> missing;
            for (const auto& [name, kf] : spatial.sources) {
                if (sources.find(name) == sources.end())
                    missing.push_back(name);
            }
            if (!missing.empty()) {
                std::ostringstream msg;
                msg << missing.size() << " source(s) from the LUSID scene could not be mapped to"
                    << " ADM WAV channels.\n";
                msg << "  ADM file: " << admFile.string() << "\n";
                msg << "  Unmappable sources:\n";
                for (const auto& name : missing)
                    msg << "    '" << name << "'\n";
                msg << "  Offline ADM source mapping convention:\n"
                    << "    'N.1' -> WAV channel N-1 (0-based), e.g. '1.1'->0, '2.1'->1\n"
                    << "    'LFE' -> WAV channel 3 when file has >= 4 channels\n"
                    << "  This is the offline renderer's mapping convention, not a general ADM or LUSID rule.";
                throw std::runtime_error(msg.str());
            }
        } else {
            sources = WavUtils::loadSources(sourcesFolder.string(), spatial.sources, spatial.sampleRate);
        }

        // main rendering happens here
        // this is where the degrees conversion and channel mapping fixes are critical
        std::cout << "Rendering...\n";
        SpatialRenderer renderer(layout, spatial, sources, routeMap);
        MultiWavData output = renderer.render(config);

        // output now uses layout deviceChannel indices with silent gaps preserved
        // if you need AlloSphere hardware channel numbers with gaps you can remap later
        std::cout << "Writing output WAV: " << outFile.string() << "\n";
        WavUtils::writeMultichannelWav(outFile.string(), output);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        std::cerr << "  Scene:  " << positionsFile.string() << "\n";
        if (useADM)
            std::cerr << "  ADM:    " << admFile.string() << "\n";
        std::cerr << "  Output: " << outFile.string() << "\n";
        if (!cultTempDir.empty()) {
            std::cerr << "  Temp dir preserved: " << cultTempDir.string() << "\n";
            cultTempDir.clear();
        }
        return 1;
    }

    // Clean up temp dir created by CULT invocation (unless --keep-temp-dir)
    if (!cultTempDir.empty()) {
        if (keepTempDir) {
            std::cout << "Temp dir preserved (--keep-temp-dir): " << cultTempDir.string() << "\n";
        } else {
            std::error_code ec;
            fs::remove_all(cultTempDir, ec);
        }
    }

    std::cout << "Done.\n";
    return 0;
}
