import subprocess
import os
from pathlib import Path


def deleteRenderOutput(output_file="processedData/spatial_render.wav"):
    #also calls delete render 
    """
    Delete the rendered output file if it exists.
    
    Parameters:
    -----------
    output_file : str
        Path to the output file to delete
    
    Returns:
    --------
    bool
        True if file was deleted or didn't exist, False on error
    """
    project_root = Path(__file__).parent.parent.resolve()
    output_path = (project_root / output_file).resolve()
    
    try:
        if output_path.exists():
            output_path.unlink()
            print(f"Deleted existing render: {output_path}")
            return True
        else:
            print(f"No existing render to delete at: {output_path}")
            return True
    except Exception as e:
        print(f"Error deleting file: {e}")
        return False


def runSpatialRender(
    source_folder="processedData/stageForRender",
    adm_file=None,
    render_instructions="processedData/stageForRender/scene.lusid.json",
    speaker_layout="spatial_engine/speaker_layouts/allosphere_layout.json",
    output_file="processedData/spatial_render.wav",
    spatializer="dbap",
    dbap_focus=1.5,
    lbap_dispersion=0.5,
    master_gain=0.5  # New parameter for master gain
):
    """
    Run the spatial renderer with the specified spatializer.
    
    Supports three spatializers:
    - dbap (default): Distance-Based Amplitude Panning - works with any layout
    - vbap: Vector Base Amplitude Panning - best for layouts with good 3D coverage
    - lbap: Layer-Based Amplitude Panning - designed for multi-ring layouts
    
    Parameters:
    -----------
    source_folder : str
        Directory containing mono source WAV files (X.1.wav, LFE.wav)
    adm_file : str, optional
        Multichannel ADM WAV file (direct streaming, skips stem splitting)
    render_instructions : str
        LUSID scene JSON file with spatial position data (scene.lusid.json)
    speaker_layout : str
        JSON file with speaker configuration
    output_file : str
        Output multichannel WAV file path
    spatializer : str
        Spatializer type: 'dbap' (default), 'vbap', or 'lbap'
    dbap_focus : float
        DBAP focus/rolloff exponent (default: 1.5, range: 0.2-5.0)
    lbap_dispersion : float
        LBAP dispersion threshold (default: 0.5, range: 0.0-1.0)
    master_gain : float
        Master gain applied to the output (default: 0.5, range: 0.0-1.0)
    
    Returns:
    --------
    bool
        True if render succeeded, False otherwise
    """
    # Get absolute paths
    project_root = Path(__file__).parent.parent.resolve()

    deleteRenderOutput(output_file)
    executable = project_root / "spatial_engine" / "spatialRender" / "build" / "spatialroot_spatial_render"
    
    # Check if executable exists
    if not executable.exists():
        print(f"Error: Executable not found at {executable}")
        print("Run setupCppTools() from src.config.configCPP to build the renderer")
        return False
    
    # Validate input mode
    useADM = adm_file is not None
    if useADM:
        if not Path(adm_file).exists():
            print(f"Error: ADM file not found: {adm_file}")
            return False
        source_desc = f"ADM file: {adm_file} (direct streaming)"
    else:
        if not Path(source_folder).exists():
            print(f"Error: Source folder not found: {source_folder}")
            return False
        source_desc = f"Source folder: {source_folder}"
    
    # Make paths absolute
    if not useADM:
        source_folder = str((project_root / source_folder).resolve())
    else:
        adm_file = str((project_root / adm_file).resolve())
    render_instructions = str((project_root / render_instructions).resolve())
    speaker_layout = str((project_root / speaker_layout).resolve())
    output_file = str((project_root / output_file).resolve())
    # Create output directory if it doesn't exist
    output_dir = Path(output_file).parent
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Check common inputs
    if not Path(render_instructions).exists():
        print(f"Error: Render instructions not found: {render_instructions}")
        return False
    if not Path(speaker_layout).exists():
        print(f"Error: Speaker layout not found: {speaker_layout}")
        return False
    
    # Validate spatializer
    valid_spatializers = ['dbap', 'vbap', 'lbap']
    if spatializer not in valid_spatializers:
        print(f"Error: Invalid spatializer '{spatializer}'. Must be one of: {valid_spatializers}")
        return False
    
    # Validate master_gain
    if not (0.0 <= master_gain <= 1.0):
        print(f"Error: Invalid master_gain '{master_gain}'. Must be in range [0.0, 1.0].")
        return False
    
    # Run the renderer
    print(f"\nRunning Spatial Renderer...")
    print(f"  Spatializer: {spatializer.upper()}")
    print(f"  {source_desc}")
    print(f"  Instructions: {render_instructions}")
    print(f"  Speaker layout: {speaker_layout}")
    print(f"  Output: {output_file}\n")
    
    # Build command
    cmd = [
        str(executable),
        "--layout", speaker_layout,
        "--positions", render_instructions,
        "--out", output_file,
        "--spatializer", spatializer
    ]
    
    # Add input source
    if useADM:
        cmd.extend(["--adm", adm_file])
    else:
        cmd.extend(["--sources", source_folder])
    
    # Add spatializer-specific parameters
    if spatializer == 'dbap':
        cmd.extend(["--dbap_focus", str(dbap_focus)])
    elif spatializer == 'lbap':
        cmd.extend(["--lbap_dispersion", str(lbap_dispersion)])
    
    # Add master_gain parameter
    cmd.extend(["--master_gain", str(master_gain)])

    print(f"  Full command: {' '.join(cmd)}")
    
    try:
        print("  Starting C++ renderer execution...")
        result = subprocess.run(
            cmd,
            check=True,
            capture_output=False,
            text=True
        )
        print("  C++ renderer finished successfully")
        
        # Check if output was created
        if Path(output_file).exists():
            size_mb = Path(output_file).stat().st_size / (1024 * 1024)
            print(f"\n✓ Render complete. Output: {output_file} ({size_mb:.1f} MB)")
            return True
        else:
            print(f"\n✗ Render failed - output file not created")
            return False
            
    except subprocess.CalledProcessError as e:
        print(f"\n✗ Render failed with error: {e}")
        return False


# Backwards compatibility alias
def runVBAPRender(
    source_folder="processedData/stageForRender",
    render_instructions="processedData/stageForRender/scene.lusid.json",
    speaker_layout="spatial_engine/speaker_layouts/allosphere_layout.json",
    output_file="processedData/spatial_render.wav"
):
    """
    DEPRECATED: Use runSpatialRender() instead.
    This function is kept for backwards compatibility and calls runSpatialRender with VBAP.
    """
    print("Note: runVBAPRender() is deprecated, use runSpatialRender(spatializer='vbap')")
    return runSpatialRender(
        source_folder=source_folder,
        render_instructions=render_instructions,
        speaker_layout=speaker_layout,
        output_file=output_file,
        spatializer='vbap'
    )


if __name__ == "__main__":
    # Run the render with default DBAP spatializer
    success = runSpatialRender()
    if success:
        print("\nVBAP render completed successfully!")
    else:
        print("\nVBAP render failed!")