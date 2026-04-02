# TODO: Convert to C++ — this script is retained for reference only. Python is no longer a project dependency.
#small file for deleting processed data files
import os
import shutil


# quick tool for cleaning up processed data
def deleteData(dir = "processedData"):
    
    if not os.path.exists(dir):
        print(f"Directory {dir} does not exist.")
        return
    
    # Get all files in the directory
    files = os.listdir(dir)
    
    if not files:
        print(f"No files found in {dir}.")
        return
    
    print(f"Deleting files in {dir}:")
    for file in files:
        file_path = os.path.join(dir, file)
        if os.path.isfile(file_path):
            os.remove(file_path)
            print(f"  Deleted: {file}")
        elif os.path.isdir(file_path):
            shutil.rmtree(file_path)
            print(f"  Deleted directory: {file}")
    
    print("Cleanup complete.")

if __name__ == "__main__":
    deleteData()