"""
runRealtime.py — Python entry point for the spatialroot Real-Time Spatial Audio Engine

Phase 3 change (2026-03-04):
  The ADM WAV preprocessing pipeline (extract metadata → parse XML → write
  scene.lusid.json) has been moved entirely into the cult-transcoder binary.
  run_realtime_from_ADM() now calls:

      cult_transcoder/build/cult-transcoder transcode
          --in <wav> --in-format adm_wav
          --out processedData/stageForRender/scene.lusid.json
          --out-format lusid_json

  cult-transcoder handles:
    - BW64 axml chunk extraction (via libbw64 submodule)
    - Writing the debug XML artifact to processedData/currentMetaData.xml
    - ADM XML → LUSID scene conversion (same parity-tested logic as Phase 2)
    - Atomic output write + report JSON at <out>.report.json

  Removed from this file:
    - extractMetaData() call (spatialroot_adm_extract subprocess)
    - parse_adm_xml_to_lusid_scene() call (Python oracle)
    - writeSceneOnly() call
    - channelHasAudio() / exportAudioActivity() / scan_audio logic
      (containsAudio is not used by CULT — all channels assumed active)
    - soundfile import (was only used for synthetic channel count)

  runPipeline.py is DEPRECATED (kept for reference, do not modify).
  It still imports extractMetaData and the Python oracle — those remain
  in place for that file only and must not be removed until runPipeline.py
  is formally retired.

Input types:
  1. ADM WAV file  → cult-transcoder produces scene.lusid.json → launch engine
  2. LUSID package directory (already has scene.lusid.json + mono WAVs)
     → validate → launch real-time engine directly (no preprocessing)

Usage (CLI):
    # From ADM source:
    python runRealtime.py sourceData/driveExampleSpruce.wav spatial_engine/speaker_layouts/allosphere_layout.json

    # From LUSID package:
    python runRealtime.py sourceData/lusid_package spatial_engine/speaker_layouts/allosphere_layout.json

Usage (from GUI or other Python code):
    from runRealtime import run_realtime_from_ADM, run_realtime_from_LUSID
    success = run_realtime_from_LUSID(
        "sourceData/lusid_package",
        "spatial_engine/speaker_layouts/allosphere_layout.json"
    )
"""

import subprocess
import signal
import sys
import os
from pathlib import Path

from src.config.configCPP import setupCppTools

# ---------------------------------------------------------------------------
# REMOVED IMPORTS (Phase 3 — 2026-03-04):
# These were only needed by the old ADM preprocessing pipeline that has been
# moved into cult-transcoder. They are kept here as comments so it is clear
# what was removed and why.
#
#   from src.analyzeADM.extractMetadata import extractMetaData
#       → spatialroot_adm_extract subprocess; now replaced by cult-transcoder
#         --in-format adm_wav which owns BW64 extraction directly.
#
#   from src.analyzeADM.checkAudioChannels import channelHasAudio, exportAudioActivity
#       → containsAudio scanning is not used in the realtime pipeline.
#         cult-transcoder assumes all channels active (AGENTS-CULT §4).
#
#   from src.packageADM.packageForRender import packageForRender, writeSceneOnly
#       → writeSceneOnly() wrote scene.lusid.json from the Python oracle.
#         cult-transcoder now writes this file directly.
#         Stem splitting (packageForRender) was never used by the realtime
#         pipeline — engine reads directly from the ADM WAV.
# ---------------------------------------------------------------------------


# ─────────────────────────────────────────────────────────────────────────────
# Core engine launcher (shared by both ADM and LUSID paths)
# ─────────────────────────────────────────────────────────────────────────────

def _launch_realtime_engine(
    scene_json,
    speaker_layout,
    sources_folder=None,
    adm_file=None,
    samplerate=48000,
    buffersize=512,
    gain=0.5,
    dbap_focus=1.5,
    remap_csv=None,
    osc_port=9009
):
    """
    Launch the C++ real-time engine as a subprocess.

    This is the final step of both the ADM and LUSID pipelines. It takes
    already-prepared paths (scene JSON, source input, speaker layout)
    and launches the C++ executable.
        executable = (
            project_root
            / "spatial_engine"
            / "realtimeEngine"
            / "build"
            / "spatialroot_realtime"
        )
    scene_json : str or Path
        Path to scene.lusid.json (positions/trajectories).
    speaker_layout : str or Path
        print("║     spatialroot Real-Time Engine — Launching             ║")
    sources_folder : str or Path, optional
        Path to folder containing mono source WAV files (X.1.wav, LFE.wav).
    adm_file : str or Path, optional
        Path to multichannel ADM WAV file for direct streaming (skips stem splitting).
    samplerate : int
        Audio sample rate in Hz (default: 48000).
    buffersize : int
        Frames per audio callback (default: 512).
    gain : float
        Master gain 0.0–1.0 (default: 0.5).
    dbap_focus : float
        DBAP focus/rolloff exponent (default: 1.5, range: 0.2–5.0).
    remap_csv : str or Path, optional
        CSV file mapping internal layout channels to physical device output
        channels (default: None = identity, no remapping).
        CSV format: 'layout,device' columns, 0-based indices, header row required.
    osc_port : int
        UDP port for al::ParameterServer OSC control from the GUI
        (default: 9009). Must match the port the GUI sends to.

    Returns
    -------
    bool
        True if the engine ran and exited cleanly, False on error.

    Notes
    -----
    Output channel count is derived automatically from the speaker layout
    by the C++ engine (Spatializer::init). No channel count parameter needed.
    """

    # Validate mutually exclusive inputs
    if not sources_folder and not adm_file:
        print("✗ Error: Either sources_folder or adm_file must be provided.")
        return False
    if sources_folder and adm_file:
        print("✗ Error: sources_folder and adm_file are mutually exclusive.")
        return False

    use_adm = adm_file is not None

    project_root = Path(__file__).parent.resolve()

    executable = (
        project_root
        / "spatial_engine"
        / "realtimeEngine"
        / "build"
        / "spatialroot_realtime"
    )

    if not executable.exists():
        print(f"✗ Error: Realtime engine executable not found at {executable}")
        print("  Build it with:")
        print("    cd spatial_engine/realtimeEngine/build && cmake .. && make -j4")
        return False

    # Resolve paths
    scene_path = Path(scene_json).resolve()
    layout_path = Path(speaker_layout).resolve()

    # Validate common paths
    if not scene_path.exists():
        print(f"✗ Error: Scene file not found: {scene_path}")
        return False
    if not layout_path.exists():
        print(f"✗ Error: Speaker layout not found: {layout_path}")
        return False
    if not (0.1 <= gain <= 3.0):
        print(f"✗ Error: Invalid gain '{gain}'. Must be in range [0.1, 3.0].")
        return False
    if not (0.2 <= dbap_focus <= 5.0):
        print(f"✗ Error: Invalid dbap_focus '{dbap_focus}'. Must be in range [0.2, 5.0].")
        return False

    # Build command based on input mode
    cmd = [
        str(executable),
        "--layout", str(layout_path),
        "--scene", str(scene_path),
        "--samplerate", str(samplerate),
        "--buffersize", str(buffersize),
        "--gain", str(gain),
        "--focus", str(dbap_focus),
        "--osc_port", str(osc_port),
    ]

    if use_adm:
        adm_path = Path(adm_file).resolve()
        if not adm_path.exists():
            print(f"✗ Error: ADM file not found: {adm_path}")
            return False
        cmd.extend(["--adm", str(adm_path)])
        source_label = f"ADM:  {adm_path} (direct streaming)"
    else:
        sources_path = Path(sources_folder).resolve()
        if not sources_path.exists():
            print(f"✗ Error: Sources folder not found: {sources_path}")
            return False
        cmd.extend(["--sources", str(sources_path)])
        source_label = f"Sources: {sources_path} (mono files)"

    if remap_csv is not None:
        remap_path = Path(remap_csv).resolve()
        if not remap_path.exists():
            print(f"✗ Error: Remap CSV not found: {remap_path}")
            return False
        cmd.extend(["--remap", str(remap_path)])

    # Print launch info
    print("\n╔══════════════════════════════════════════════════════════╗")
    print("║     spatialroot Real-Time Engine — Launching               ║")
    print("╚══════════════════════════════════════════════════════════╝\n")
    print(f"  Scene:          {scene_path}")
    print(f"  {source_label}")
    print(f"  Speaker layout: {layout_path}")
    print(f"  Sample rate:    {samplerate} Hz")
    print(f"  Buffer size:    {buffersize} frames")
    print(f"  Master gain:    {gain}")
    print(f"  DBAP focus:     {dbap_focus}")
    print(f"  OSC port:       {osc_port}")
    if remap_csv is not None:
        print(f"  Remap CSV:      {remap_path}")
    print(f"  (Output channels derived from speaker layout)")
    print(f"\n  Command: {' '.join(cmd)}\n")

    # Launch — engine runs until Ctrl+C. We forward SIGINT so it shuts down cleanly.
    try:
        print("  Starting real-time engine...")
        print("  Press Ctrl+C to stop.\n")

        process = subprocess.Popen(cmd)
        process.wait()

        exit_code = process.returncode
        if exit_code == 0:
            print("\n✓ Real-time engine exited cleanly.")
            return True
        elif exit_code == -2 or exit_code == 130:
            # SIGINT (Ctrl+C) — normal exit path
            print("\n✓ Real-time engine stopped by user (Ctrl+C).")
            return True
        else:
            print(f"\n✗ Real-time engine exited with code {exit_code}")
            return False

    except KeyboardInterrupt:
        print("\n  Ctrl+C caught — stopping engine...")
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            print("  Engine did not stop in time, killing...")
            process.kill()
        print("✓ Real-time engine stopped.")
        return True

    except FileNotFoundError:
        print(f"\n✗ Could not launch executable: {executable}")
        print("  Make sure the realtime engine is built.")
        return False

    except Exception as e:
        print(f"\n✗ Unexpected error: {e}")
        return False


# ─────────────────────────────────────────────────────────────────────────────
# Pipeline entry points (mirror runPipeline.py)
# ─────────────────────────────────────────────────────────────────────────────

def run_realtime_from_ADM(
    source_adm_file,
    source_speaker_layout,
    master_gain=0.5,
    dbap_focus=1.5,
    samplerate=48000,
    buffersize=512,
    remap_csv=None,
    osc_port=9009
):
    """
    Run the complete ADM WAV → LUSID → real-time spatial audio pipeline.

    Phase 3 (2026-03-04): preprocessing now delegated entirely to cult-transcoder.
    This function:
      1. Checks initialization flag
      2. Calls setupCppTools() to verify/build C++ tools (idempotent)
      3. Calls cult-transcoder to extract axml from BW64 WAV, convert to LUSID,
         and write processedData/stageForRender/scene.lusid.json atomically
      4. Launches the real-time C++ engine (ADM direct streaming mode)

    REMOVED parameters vs. previous version:
      scan_audio  — contained channels analysis logic now inside cult-transcoder.
                    cult-transcoder assumes all channels active (AGENTS-CULT §4).
                    The --scan_audio CLI flag is also removed.

    REMOVED preprocessing steps vs. previous version:
      - exportAudioActivity() / channelHasAudio()  — see scan_audio note above
      - soundfile channel count query (was only for synthetic contains_audio_data)
      - extractMetaData() subprocess (spatialroot_adm_extract binary)
        → replaced by cult-transcoder --in-format adm_wav BW64 reader
      - parse_adm_xml_to_lusid_scene() Python oracle call
        → replaced by cult-transcoder's parity-tested adm_to_lusid converter
      - writeSceneOnly() call
        → cult-transcoder writes scene.lusid.json atomically (tmp + rename)

    Parameters
    ----------
    source_adm_file : str
        Path to source ADM WAV (BW64) file.
    source_speaker_layout : str
        Path to speaker layout JSON.
    master_gain : float
        Master gain 0.0–1.0 (default: 0.5).
    dbap_focus : float
        DBAP focus/rolloff exponent (default: 1.5).
    samplerate : int
        Audio sample rate in Hz (default: 48000).
    buffersize : int
        Frames per audio callback (default: 512).
    remap_csv : str or Path, optional
        CSV file for output channel remapping (default: None = identity).
    osc_port : int
        UDP port for GUI OSC control (default: 9009).

    Returns
    -------
    bool
        True if the engine ran and exited cleanly, False on error.
    """

    # Step 0: Check initialization
    project_root = Path(__file__).parent.resolve()
    init_flag = project_root / ".init_complete"
    if not init_flag.exists():
        print("\n" + "!" * 80)
        print("⚠ WARNING: Project not initialized!")
        print("!" * 80)
        print("\nPlease run: ./init.sh")
        return False

    # Ensure output directories exist
    os.makedirs(project_root / "processedData", exist_ok=True)
    os.makedirs(project_root / "processedData" / "stageForRender", exist_ok=True)

    # Step 1: Setup C++ tools (idempotent — only builds if needed)
    print("\n" + "=" * 80)
    print("STEP 1: Verifying C++ tools and dependencies")
    print("=" * 80)
    if not setupCppTools():
        print("\n✗ Error: C++ tools setup failed")
        print("\nTry re-initializing:")
        print("  rm .init_complete && ./init.sh")
        return False

    # Step 2: Extract ADM metadata and produce scene.lusid.json via cult-transcoder.
    #
    # cult-transcoder handles everything that Steps 2–4 used to do in Python:
    #   - Opens the BW64 WAV and reads the axml chunk (via libbw64 submodule)
    #   - Writes the debug XML artifact to processedData/currentMetaData.xml
    #   - Converts ADM XML → LUSID scene (same parity-tested logic as Phase 2)
    #   - Writes scene.lusid.json atomically: <out>.tmp then rename to final path
    #   - Writes <out>.report.json alongside the LUSID file
    #
    # REMOVED (Phase 3 — 2026-03-04):
    #   Step 2 previously called exportAudioActivity() + channelHasAudio() or built
    #   a synthetic "all channels active" dict, then queried soundfile for channel count.
    #   cult-transcoder assumes all channels active (see AGENTS-CULT §4).
    #   containsAudio.json is NOT written or consulted by the realtime pipeline.
    #
    #   Step 3 previously called parse_adm_xml_to_lusid_scene() (Python LUSID oracle)
    #   Step 4 previously called writeSceneOnly() from src.packageADM.packageForRender.
    #   Both are superseded by cult-transcoder.
    #
    print("\n" + "=" * 80)
    print("STEP 2: ADM extraction + LUSID generation (cult-transcoder Phase 3)")
    print("=" * 80)

    cult_binary = project_root / "cult_transcoder" / "build" / "cult-transcoder"
    scene_json_path = "processedData/stageForRender/scene.lusid.json"

    if not cult_binary.exists():
        print(f"✗ Error: cult-transcoder binary not found at {cult_binary}")
        print("  Build it with:")
        print("    cd cult_transcoder/build && cmake .. && make -j4")
        return False

    print(f"  Input:  {source_adm_file}")
    print(f"  Output: {scene_json_path}")
    print(f"  Binary: {cult_binary}")

    transcode_result = subprocess.run(
        [
            str(cult_binary), "transcode",
            "--in",         source_adm_file,
            "--in-format",  "adm_wav",
            "--out",        scene_json_path,
            "--out-format", "lusid_json",
        ],
        check=False,
    )

    if transcode_result.returncode != 0:
        print(f"\n✗ Error: cult-transcoder exited with code {transcode_result.returncode}")
        print("  Check above output for details.")
        print("  Common causes:")
        print("    - WAV file has no axml chunk (not an ADM BW64 file)")
        print("    - ADM XML failed to parse (malformed metadata)")
        print("    - Output path not writable (check processedData/stageForRender/)")
        return False

    print(f"✓ scene.lusid.json written to: {scene_json_path}")

    # Step 3: Launch real-time engine in ADM direct streaming mode.
    #
    # NOTE: Previously Step 5. Renumbered because Steps 2–4 are now a single
    # cult-transcoder call. Functionality is identical.
    print("\n" + "=" * 80)
    print("STEP 3: Launching real-time engine (ADM direct streaming)")
    print("=" * 80)
    return _launch_realtime_engine(
        scene_json=scene_json_path,
        speaker_layout=source_speaker_layout,
        adm_file=source_adm_file,
        samplerate=samplerate,
        buffersize=buffersize,
        gain=master_gain,
        dbap_focus=dbap_focus,
        remap_csv=remap_csv,
        osc_port=osc_port
    )


def run_realtime_from_LUSID(
    source_lusid_package,
    source_speaker_layout,
    master_gain=0.5,
    dbap_focus=1.5,
    samplerate=48000,
    buffersize=512,
    remap_csv=None,
    osc_port=9009
):
    """
    Run real-time engine from an existing LUSID package.

    Same validation as src/createFromLUSID.run_pipeline_from_LUSID but launches
    the real-time engine instead of the offline renderer. No preprocessing
    needed — the LUSID package already contains scene.lusid.json + mono WAVs.

    Parameters
    ----------
    source_lusid_package : str
        Path to LUSID package directory (must contain scene.lusid.json).
    source_speaker_layout : str
        Path to speaker layout JSON.
    master_gain : float
        Master gain 0.0–1.0 (default: 0.5).
    dbap_focus : float
        DBAP focus/rolloff exponent (default: 1.5).
    samplerate : int
        Audio sample rate in Hz (default: 48000).
    buffersize : int
        Frames per audio callback (default: 512).
    remap_csv : str or Path, optional
        CSV file for output channel remapping (default: None = identity).
    osc_port : int
        UDP port for GUI OSC control (default: 9009).

    Returns
    -------
    bool
        True if the engine ran and exited cleanly, False on error.
    """

    # Validate LUSID package
    package_path = Path(source_lusid_package)
    if not package_path.exists():
        print(f"✗ Error: LUSID package directory not found: {source_lusid_package}")
        return False

    scene_file = package_path / "scene.lusid.json"
    if not scene_file.exists():
        print(f"✗ Error: scene.lusid.json not found in package: {scene_file}")
        return False

    layout_path = Path(source_speaker_layout)
    if not layout_path.exists():
        print(f"✗ Error: Speaker layout file not found: {source_speaker_layout}")
        return False

    print(f"✓ LUSID package: {package_path}")
    print(f"✓ Scene file:    {scene_file}")
    print(f"✓ Speaker layout: {layout_path}")

    # Launch real-time engine directly (no preprocessing needed)
    return _launch_realtime_engine(
        scene_json=str(scene_file),
        speaker_layout=str(layout_path),
        sources_folder=str(package_path),
        samplerate=samplerate,
        buffersize=buffersize,
        gain=master_gain,
        dbap_focus=dbap_focus,
        remap_csv=remap_csv,
        osc_port=osc_port
    )


# ─────────────────────────────────────────────────────────────────────────────
# Source type detection (same logic as runPipeline.py)
# ─────────────────────────────────────────────────────────────────────────────

def checkSourceType(arg):
    """
    Detect whether the input is an ADM WAV file or a LUSID package directory.

    Returns 'ADM' for .wav files, 'LUSID' for directories containing
    scene.lusid.json, or an error string otherwise.
    """
    if not os.path.exists(arg):
        return "Path does not exist"

    if os.path.isfile(arg):
        if arg.lower().endswith('.wav'):
            return "ADM"

    if os.path.isdir(arg):
        # Check for scene.lusid.json inside the directory (more robust
        # than checking basename — works for any package directory name)
        if os.path.exists(os.path.join(arg, "scene.lusid.json")):
            return "LUSID"

    return "Wrong Input Type"


# ─────────────────────────────────────────────────────────────────────────────
# CLI entry point
# ─────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("spatialroot Real-Time Engine Launcher")
    print("=" * 60)

    if len(sys.argv) >= 2:
        source_input = sys.argv[1]
        source_type = checkSourceType(source_input)
        source_speaker_layout = sys.argv[2] if len(sys.argv) >= 3 else "spatial_engine/speaker_layouts/allosphere_layout.json"
        master_gain = float(sys.argv[3]) if len(sys.argv) >= 4 else 0.5
        dbap_focus = float(sys.argv[4]) if len(sys.argv) >= 5 else 1.5
        buffersize = int(sys.argv[5]) if len(sys.argv) >= 6 else 512
        # REMOVED (Phase 3 — 2026-03-04): scan_audio flag no longer exists.
        # cult-transcoder handles channel analysis internally (all channels assumed active).
        # scan_audio = "--scan_audio" in sys.argv  ← removed

        # Named optional args: --remap and --osc_port
        remap_csv = None
        osc_port = 9009
        args = sys.argv[1:]
        for i, a in enumerate(args):
            if a == "--remap" and i + 1 < len(args):
                remap_csv = args[i + 1]
            elif a == "--osc_port" and i + 1 < len(args):
                try:
                    osc_port = int(args[i + 1])
                except ValueError:
                    print(f"✗ Error: --osc_port must be an integer, got '{args[i + 1]}'")
                    sys.exit(1)

        if source_type == "ADM":
            print(f"Detected ADM source: {source_input}")
            success = run_realtime_from_ADM(
                source_input, source_speaker_layout,
                master_gain=master_gain, dbap_focus=dbap_focus,
                buffersize=buffersize,
                remap_csv=remap_csv, osc_port=osc_port
            )
        elif source_type == "LUSID":
            print(f"Detected LUSID package: {source_input}")
            success = run_realtime_from_LUSID(
                source_input, source_speaker_layout,
                master_gain=master_gain, dbap_focus=dbap_focus,
                buffersize=buffersize,
                remap_csv=remap_csv, osc_port=osc_port
            )
        elif source_type == "Path does not exist":
            print(f"✗ Error: Path does not exist: {source_input}")
            success = False
        else:
            print(f"✗ Error: Unrecognized input type for: {source_input}")
            print("  Expected: ADM WAV file (.wav) or LUSID package directory (containing scene.lusid.json)")
            success = False

        sys.exit(0 if success else 1)

    else:
        print("\nUsage:")
        print("  python runRealtime.py <source> [speaker_layout] [master_gain] [dbap_focus] [buffersize]")
        print("                        [--remap <csv>] [--osc_port <port>]")
        print("\nArguments:")
        print("  <source>              ADM WAV file (.wav) or LUSID package directory")
        print("  [speaker_layout]      Speaker layout JSON (default: allosphere_layout.json)")
        print("  [master_gain]         Master gain 0.1–3.0 (default: 0.5)")
        print("  [dbap_focus]          DBAP focus/rolloff 0.2–5.0 (default: 1.5)")
        print("  [buffersize]          Audio buffer size in frames (default: 512)")
        # REMOVED (Phase 3 — 2026-03-04): --scan_audio flag removed.
        # cult-transcoder handles extraction internally; all channels assumed active.
        # print("  [--scan_audio]        Run full per-channel audio activity scan before")
        # print("                        parsing ADM metadata (ADM path only, default: OFF).")
        # print("                        Adds ~14s startup time but filters truly silent channels.")
        print("  [--remap <csv>]       CSV file for output channel remapping (optional).")
        print("                        Format: 'layout,device' columns, 0-based, header required.")
        print("  [--osc_port <port>]   UDP port for GUI OSC control (default: 9009).")
        print("\nExamples:")
        print("  # From ADM WAV (cult-transcoder handles preprocessing):")
        print("  python runRealtime.py sourceData/driveExampleSpruce.wav")
        print("")
        # REMOVED (Phase 3 — 2026-03-04): --scan_audio example removed.
        # print("  # From ADM WAV with audio scan enabled:")
        # print("  python runRealtime.py sourceData/driveExampleSpruce.wav allosphere_layout.json 0.5 1.5 512 --scan_audio")
        # print("")
        print("  # From LUSID package (skips preprocessing entirely):")
        print("  python runRealtime.py sourceData/lusid_package")
        print("")
        print("  # With custom layout, remap CSV, and non-default OSC port:")
        print("  python runRealtime.py sourceData/lusid_package spatial_engine/speaker_layouts/allosphere_layout.json 0.3 1.5 256 --remap myRemap.csv --osc_port 9010")
        print("\nNote: Output channels are derived automatically from the speaker layout.")
        sys.exit(1)
