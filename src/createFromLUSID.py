from src.config.configCPP import setupCppTools
from src.createRender import runSpatialRender
from src.analyzeRender import analyzeRenderOutput
from pathlib import Path
import sys


# Current pipeline for LUSID packages:
# 0. Check initialization - if not initialized, prompt to run ./init.sh
# 1. Setup C++ tools - build spatial renderer (only if needed)
# 2. Run spatial renderer - reads LUSID scene directly via C++ JSONLoader
# 3. Analyze render output - create PDF with dB analysis of each channel in final render


def check_initialization():
    """
    Check if init.sh has been run by looking for .init_complete flag file.
    
    Returns:
    --------
    bool
        True if initialized, False otherwise
    """
    project_root = Path(__file__).parent.parent.resolve()  # Go up two levels: src/ -> project_root/
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
    print("  3. Setup C++ tools (allolib, embedded ADM extractor, VBAP renderer)")
    print("\nAfter initialization, run the pipeline again.")
    print("="*80 + "\n")
    return False


def run_pipeline_from_LUSID(source_lusid_package, source_speaker_layout, spatializer="dbap", create_render_analysis=True, outputRenderPath="processedData/spatial_render.wav", **kwargs):
    """
    Run the complete LUSID package to spatial audio pipeline
    
    Args:
        source_lusid_package: path to source LUSID package directory
        source_speaker_layout: path to speaker layout JSON
        create_render_analysis: whether to create render analysis PDF
        spatializer: spatializer type ('dbap', 'vbap', 'lbap')
    """
    # Step 0: Check if project has been initialized
    if not check_initialization():
        return False
    
    # Step 1: Setup C++ tools and dependencies (only runs if needed - idempotent)
    print("\n" + "="*80)
    print("STEP 1: Verifying C++ tools and dependencies")
    print("="*80)
    if not setupCppTools():
        print("\n✗ Error: C++ tools setup failed")
        print("\nTry re-initializing:")
        print("  rm .init_complete && ./init.sh")
        return False
    
    # Step 2: Basic validation of inputs
    print("\n" + "="*80)
    print("STEP 2: Validating inputs")
    print("="*80)
    
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
    print(f"✓ Scene file: {scene_file}")
    print(f"✓ Speaker layout: {layout_path}")

    # Set up output paths
    final_output_render_file = outputRenderPath
    final_output_render_analysis_pdf = outputRenderPath.replace(".wav", ".pdf")

    # Step 3: Run spatial renderer
    # The C++ renderer handles all the complex LUSID scene parsing, audio file loading, etc.
    print("\n" + "="*80)
    print("STEP 3: Running spatial renderer")
    print("="*80)
    
    # Accept extra kwargs for spatializer params (e.g., master_gain, dbap_focus, lbap_dispersion)
    success = runSpatialRender(
        source_folder=str(package_path),
        render_instructions=str(scene_file),
        speaker_layout=source_speaker_layout,
        output_file=final_output_render_file,
        spatializer=spatializer,
        **kwargs
    )
    
    if not success:
        print("✗ Spatial rendering failed")
        return False

    # Step 4: Analyze render output (optional)
    if create_render_analysis:
        print("\n" + "="*80)
        print("STEP 4: Analyzing rendered spatial audio")
        print("="*80)
        
        analyzeRenderOutput(
            render_file=final_output_render_file,
            output_pdf=final_output_render_analysis_pdf
        )

    print("\n" + "="*80)
    print("Pipeline completed successfully!")
    print("="*80)
    print(f"✓ Spatial render: {final_output_render_file}")
    if create_render_analysis:
        print(f"✓ Analysis PDF: {final_output_render_analysis_pdf}")
    
    return True


if __name__ == "__main__":
    # CLI mode - parse arguments
    if len(sys.argv) >= 2:
        source_lusid_package = sys.argv[1]
        source_speaker_layout = sys.argv[2] if len(sys.argv) >= 3 else "spatial_engine/speaker_layouts/allosphere_layout.json"
        create_render_analysis = True if len(sys.argv) < 4 else sys.argv[3].lower() in ['true', '1', 'yes']
        spatializer = sys.argv[4] if len(sys.argv) >= 5 else "dbap"
        outputRenderPath = sys.argv[5] if len(sys.argv) >= 6 else "processedData/spatial_render.wav"

        # Optionally parse extra spatializer kwargs from CLI (future-proofing)
        extra_kwargs = {}
        # Example: python createFromLUSID.py ... ... ... ... ... master_gain=0.5
        for arg in sys.argv[6:]:
            if '=' in arg:
                k, v = arg.split('=', 1)
                try:
                    v = float(v)
                except ValueError:
                    pass
                extra_kwargs[k] = v

        success = run_pipeline_from_LUSID(source_lusid_package, source_speaker_layout, spatializer, create_render_analysis, outputRenderPath, **extra_kwargs)
        sys.exit(0 if success else 1)
    
    else:
        # Default mode - show usage and example
        print("Usage: python createFromLUSID.py <sourceLUSIDPackage> [sourceSpeakerLayout] [createAnalysis] [spatializer] [outputRenderPath] [spatializer_kwargs...]")
        print("\nParameters:")
        print("  sourceLUSIDPackage:   Path to LUSID package directory")
        print("  sourceSpeakerLayout:  Speaker layout JSON file (default: allosphere_layout.json)")
        print("  createAnalysis:       Create render analysis PDF (default: true)")
        print("  spatializer:          Spatializer type: dbap, vbap, lbap (default: dbap)")
        print("  outputRenderPath:     Output WAV file path (default: processedData/spatial_render.wav)")
        print("  spatializer_kwargs:   Additional spatializer parameters as key=value pairs (e.g., master_gain=0.5)")
        print("\nExample:")
        print("  python createFromLUSID.py sourceData/lusid_package")
        print("  python createFromLUSID.py sourceData/lusid_package spatial_engine/speaker_layouts/custom_layout.json false vbap processedData/my_render.wav master_gain=0.7 dbap_focus=1.2")
        print("\nA LUSID package should contain:")
        print("  - scene.lusid.json (required)")
        print("  - Audio files: X.1.wav, LFE.wav, etc.")
        print("  - containsAudio.json (optional)")
        print("  - mir_summary.json (optional)")
        
        print("\nNote: The C++ spatial renderer handles all LUSID scene parsing and audio file loading.")
        print("This Python script just routes paths and calls the renderer.")