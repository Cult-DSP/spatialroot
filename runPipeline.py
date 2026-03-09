from src.config.configCPP import setupCppTools
from src.createRender import runSpatialRender
from src.analyzeRender import analyzeRenderOutput
from src.createFromLUSID import run_pipeline_from_LUSID
from pathlib import Path
import subprocess
import sys
import os

# Define outputRenderPath at the top level for global accessibility
outputRenderPath = "processedData/outputRender.wav"


# Current pipeline:
# 0. Check initialization - if not initialized, prompt to run ./init.sh
# 1. Setup C++ tools - initialize git submodules (allolib, libbw64, libadm), build spatial renderer and ADM extractor (only if needed)
# 2. Extract ADM metadata from source WAV using cult-transcoder
# 3. Parse ADM metadata into internal data structure (optionally export JSON for analysis)
# 4. Analyze audio channels for content (generate containsAudio.json)
# 5. Run packageForRender - split stems (X.1.wav naming) and build LUSID scene (scene.lusid.json)
# 6. Run spatial renderer - create multichannel spatial render (reads LUSID scene directly)
# 7. Analyze render output - create PDF with dB analysis of each channel in final render


def check_initialization():
    """
    Check if init.sh has been run by looking for .init_complete flag file.
    
    Returns:
    --------
    bool
        True if initialized, False otherwise
    """
    project_root = Path(__file__).parent.resolve()
    init_flag = project_root / ".init_complete"
    
    if init_flag.exists():
        return True
    
    print("\n" + "!"*80)
    print("⚠ WARNING: Project not initialized!")
    print("!"*80)
    print("\nPlease run the initialization script first:")
    print("  ./init.sh")
    print("\nThis will:")
    print("  1. Create Python virtual environment")
    print("  2. Install Python dependencies")
    print("  3. Setup C++ tools (allolib, ADM extractor, spatial renderer)")
    print("\nAfter initialization, run the pipeline again.")
    print("="*80 + "\n")
    return False



def run_pipeline_from_ADM(sourceADMFile, sourceSpeakerLayout, renderMode="dbap", resolution=1.5, createRenderAnalysis=True, master_gain=0.5, outputRenderPath="processedData/spatial_render.wav"):
    """
    Run the complete ADM to spatial audio pipeline
    
    Args:
        sourceADMFile: path to source ADM WAV file
        sourceSpeakerLayout: path to speaker layout JSON
        renderMode: spatializer type (default: "dbap")
        resolution: spatializer-specific parameter (e.g., dbap_focus or lbap_dispersion)
        createRenderAnalysis: whether to create render analysis PDF
        master_gain: master gain for the renderer (default: 0.5)
    """
    # Step 0: Check if project has been initialized
    if not check_initialization():
        return False
    
    # Step 1: Setup C++ tools and dependencies (only runs if needed - idempotent)
    # Note: If you encounter dependency errors, delete .init_complete and re-run ./init.sh
    print("\n" + "="*80)
    print("STEP 1: Verifying C++ tools and dependencies")
    print("="*80)
    if not setupCppTools():
        print("\n✗ Error: C++ tools setup failed")
        print("\nTry re-initializing:")
        print("  rm .init_complete && ./init.sh")
        return False
    
    processedDataDir = "processedData"
    finalOutputRenderFile = outputRenderPath
    finalOutputRenderAnalysisPDF = outputRenderPath.replace(".wav", ".pdf")

    # -- Extract ADM metadata and produce scene.lusid.json via cult-transcoder --
    print("\n" + "="*80)
    print("STEP 2: ADM extraction + LUSID generation (cult-transcoder)")
    print("="*80)

    cult_binary = "cult_transcoder/build/cult-transcoder"
    scene_json_path = "processedData/stageForRender/scene.lusid.json"

    if not os.path.exists(cult_binary):
        print(f"✗ Error: cult-transcoder binary not found at {cult_binary}")
        print("  Build it with:")
        print("    cd cult_transcoder/build && cmake .. && make -j4")
        return False

    print(f"  Input:  {sourceADMFile}")
    print(f"  Output: {scene_json_path}")

    transcode_result = subprocess.run(
        [
            cult_binary, "transcode",
            "--in",         sourceADMFile,
            "--in-format",  "adm_wav",
            "--out",        scene_json_path,
            "--out-format", "lusid_json",
        ],
        check=False,
    )

    if transcode_result.returncode != 0:
        print(f"\n✗ Error: cult-transcoder exited with code {transcode_result.returncode}")
        print("  Check above output for details.")
        return False

    print(f"✓ scene.lusid.json written to: {scene_json_path}")

    # -- Run spatial renderer with ADM direct input --
    print("\n" + "="*80)
    print("STEP 3: Running spatial renderer (ADM direct input)")
    print("="*80)
    spatializer = renderMode
    extra_kwargs = {}
    if renderMode == 'dbap':  # Include default "dbap" mode
        extra_kwargs['dbap_focus'] = resolution
    elif renderMode == "lbap":
        extra_kwargs['lbap_dispersion'] = resolution
    runSpatialRender(
        adm_file=sourceADMFile,
        render_instructions=scene_json_path,
        speaker_layout=sourceSpeakerLayout,
        output_file=finalOutputRenderFile,
        spatializer=spatializer,
        master_gain=master_gain,  # Pass master_gain to runSpatialRender
        **extra_kwargs
    )

    if createRenderAnalysis:
        print("\nAnalyzing rendered spatial audio...")
        analyzeRenderOutput(
            render_file=finalOutputRenderFile,
            output_pdf=finalOutputRenderAnalysisPDF
        )

    print("\nDone")

    
def checkSourceType(arg):
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


if __name__ == "__main__":
    # CLI mode - parse arguments

    if len(sys.argv) >= 2:
        sourceFile = sys.argv[1]
        sourceType = checkSourceType(sourceFile)
        sourceSpeakerLayout = sys.argv[2] if len(sys.argv) >= 3 else "spatial_engine/speaker_layouts/allosphere_layout.json"
        renderMode = sys.argv[3] if len(sys.argv) >= 4 else "dbap"
        resolution = float(sys.argv[4]) if len(sys.argv) >= 5 else 1.5
        master_gain = float(sys.argv[5]) if len(sys.argv) >= 6 else 0.5  # Added master_gain argument
        createRenderAnalysis = True if len(sys.argv) < 7 else sys.argv[6].lower() in ['true', '1', 'yes']

        if sourceType == "ADM":
            print("Running pipeline from ADM source...")
            run_pipeline_from_ADM(sourceFile, sourceSpeakerLayout, renderMode, resolution, createRenderAnalysis, master_gain)
        elif sourceType == "LUSID":
            print("Running pipeline from LUSID source...")
            # Pass outputRenderPath explicitly to run_pipeline_from_LUSID
            run_pipeline_from_LUSID(sourceFile, sourceSpeakerLayout, renderMode, createRenderAnalysis, outputRenderPath)

    else:
        # default mode
        print("Usage: python runPipeline.py <sourceFile> [sourceSpeakerLayout] [renderMode] [resolution] [master_gain] [createAnalysis]")
        print("  sourceFile: ADM WAV file or LUSID package directory")
        print("\nRunning with default configuration...")

        sourceADMFile = "sourceData/driveExampleSpruce.wav"
        sourceSpeakerLayout = "spatial_engine/speaker_layouts/allosphere_layout.json"
        master_gain = 0.5
        createRenderAnalysis = True

        run_pipeline_from_ADM(sourceADMFile, sourceSpeakerLayout, "dbap", 1.5, createRenderAnalysis, master_gain)

