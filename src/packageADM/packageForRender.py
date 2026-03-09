
from src.packageADM.splitStems import splitChannelsToMono
from LUSID.src.xml_etree_parser import write_lusid_scene

# Updated 2026-02-10: Now accepts LusidScene object directly from single-step XML parsing.
# Eliminates intermediate dicts and JSON files entirely.

# FUTURE DEVELOPMENT NOTE (2026-03-09):
# This function is now optional and disabled by default.
# With ADM direct input support in spatialroot_spatial_render,
# stem splitting is no longer needed for ADM sources.
# Consider renaming this module to "createLUSIDPackage" and
# making it a utility for generating LUSID packages from ADM sources
# when mono WAV files are explicitly desired.

def createLUSIDPackage(sourceADM, lusid_scene, contains_audio_data=None,
                       processed_dir="processedData",
                       output_dir="processedData/stageForRender",
                       split_stems=False):
    """Create a LUSID package from ADM source (optional stem splitting).

    By default, only writes the LUSID scene JSON. Stem splitting is disabled
    since the offline renderer now supports ADM direct input.

    Args:
        sourceADM (str): Path to the source multichannel ADM WAV file.
        lusid_scene (LusidScene): Pre-built LUSID scene object.
        contains_audio_data (dict, optional): Per-channel audio activity data.
            Ignored when split_stems=False.
        processed_dir (str): Directory containing processed data (for splitStems).
        output_dir (str): Directory to save packaged data.
        split_stems (bool): Whether to split ADM WAV into mono stems (default: False).
    """
    print("Creating LUSID package...")

    # Write LUSID scene directly (no intermediate processing needed)
    lusid_output = f"{output_dir}/scene.lusid.json"
    write_lusid_scene(lusid_scene, lusid_output)
    
    # Conditionally split audio stems using LUSID node ID naming
    if split_stems:
        if contains_audio_data is None:
            print("Warning: contains_audio_data required for stem splitting")
            return
        splitChannelsToMono(sourceADM, processed_dir=processed_dir, output_dir=output_dir, contains_audio_data=contains_audio_data)
        print(f"Created LUSID package with stems in {output_dir}")
    else:
        print(f"Created LUSID package (scene only) in {output_dir}")


# LEGACY FUNCTION - kept for backward compatibility
def packageForRender(sourceADM, lusid_scene, contains_audio_data,
                     processed_dir="processedData",
                     output_dir="processedData/stageForRender"):
    """Legacy function - use createLUSIDPackage with split_stems=True instead."""
    return createLUSIDPackage(sourceADM, lusid_scene, contains_audio_data,
                             processed_dir, output_dir, split_stems=True)


def writeSceneOnly(lusid_scene, output_dir="processedData/stageForRender"):
    """Write the LUSID scene JSON without splitting stems.

    Used by the real-time pipeline to skip the stem-splitting step.
    The real-time C++ engine reads directly from the multichannel ADM WAV
    file, so only the scene.lusid.json (positions/trajectories) is needed.

    Args:
        lusid_scene (LusidScene): Pre-built LUSID scene object.
        output_dir (str): Directory to save scene.lusid.json.

    Returns:
        str: Path to the written scene.lusid.json file.
    """
    import os
    os.makedirs(output_dir, exist_ok=True)
    lusid_output = f"{output_dir}/scene.lusid.json"
    write_lusid_scene(lusid_scene, lusid_output)
    print(f"✓ Wrote scene.lusid.json (no stem splitting) to {lusid_output}")
    return lusid_output


# if __name__ == "__main__":
#     packageForRender('sourceData/POE-ATMOS-FINAL.wav', processed_dir="processedData", output_dir="processedData/stageForRender")