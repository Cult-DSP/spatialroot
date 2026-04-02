# TODO: Convert to C++ — this script is retained for reference only. Python is no longer a project dependency.
#!/usr/bin/env python3
# command line tool to download basic example files

import subprocess
import sys
from pathlib import Path

# Google Drive file ID
fileID1 = "16Z73gODkZzCWjYy313FZc6ScG-CCXL4h"
outputname1 = "driveExample1.wav"
fileID2 = "1-oh0tixJV3C-odKdcM7Ak-ziCv5bNKJB"
outputname2 = "driveExample2.wav"
fileID3 = "1NsW8xj4wFGhGtSKRIuPL4E2yoEJEIq4X"
outputname3 = "driveExampleSpruce.wav"

def download_from_google_drive(file_id, output_name):
    """Download file from Google Drive using gdown"""
    import gdown
    
    # create sourceData directory if needed
    project_root = Path(__file__).parent.parent
    source_data_dir = project_root / "sourceData"
    source_data_dir.mkdir(exist_ok=True)
    
    output_path = source_data_dir / output_name
    
    # download using direct download URL
    url = f"https://drive.google.com/uc?id={file_id}"
    
    print(f"\nDownloading example file to: {output_path}")
    print("This may take a while for large files...\n")
    
    gdown.download(url, str(output_path), quiet=False)
    
    print(f"\nDownload complete!")
    print(f"Saved to: {output_path}")
    
    # verify file exists
    if output_path.exists():
        size_mb = output_path.stat().st_size / (1024 * 1024)
        print(f"File verified: {size_mb:.1f} MB")
    else:
        print(f"WARNING: File not found at {output_path}")

if __name__ == "__main__":
    try:
        import gdown
    except ImportError:
        print("Installing gdown package...")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "gdown"])
        import gdown
    download_from_google_drive(fileID1, outputname1)
    download_from_google_drive(fileID2, outputname2)
    download_from_google_drive(fileID3, outputname3)