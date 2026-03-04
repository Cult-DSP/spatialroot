import subprocess
from pathlib import Path


def get_repo_root() -> Path:
    # __file__ = repo/src/config/configCPP_posix.py
    return Path(__file__).resolve().parents[2]


def setupCppTools():
    """
    Complete setup for C++ tools and dependencies.
    Orchestrates submodule initialization, Spatial renderer build,
    and the embedded ADM extractor build.
    Only performs actions that are needed (idempotent).

    Returns:
    --------
    bool
        True if all setup succeeded, False otherwise
    """
    print("\n" + "="*60)
    print("Setting up C++ tools and dependencies...")
    print("="*60)

    # Step 1: Initialize git submodules (allolib) if needed
    if not initializeSubmodules():
        print("\n✗ Error: Failed to initialize allolib submodule")
        return False

    # Step 2: [SUPERSEDED — Phase 3 — 2026-03-04] Initialize EBU submodules (libbw64 + libadm)
    #
    # libbw64 is now a git submodule of cult_transcoder (cult_transcoder/thirdparty/libbw64).
    # spatialroot_adm_extract (src/adm_extract/) is DEPRECATED — cult-transcoder handles BW64
    # axml extraction natively via --in-format adm_wav (using its own libbw64 copy).
    # These steps are commented rather than removed pending final cleanup of src/adm_extract/.
    # See: cult_transcoder/internalDocsMD/DEV-PLAN-CULT.md Phase 3
    #      cult_transcoder/internalDocsMD/AGENTS-CULT.md §8
    #
    # if not initializeEbuSubmodules():
    #     print("\n✗ Error: EBU submodule initialization failed — cannot build ADM extractor")
    #     return False

    # Step 3: [SUPERSEDED — Phase 3 — 2026-03-04] Build the embedded ADM extractor tool
    #
    # spatialroot_adm_extract is replaced by cult-transcoder --in-format adm_wav.
    # The src/adm_extract/ CMake project is deprecated and should not be built.
    # runRealtime.py now calls cult_transcoder/build/cult-transcoder directly.
    #
    # if not buildAdmExtractor():
    #     print("\n✗ Error: ADM extractor build failed")
    #     return False

    # Step 4: Build Spatial renderer if needed
    if not buildSpatialRenderer():
        print("\n✗ Error: Failed to build Spatial renderer")
        return False

    # Step 5: Build Realtime engine if needed
    if not buildRealtimeEngine():
        print("\n✗ Error: Failed to build Realtime engine")
        return False

    print("\n" + "="*60)
    print("✓ C++ tools setup complete!")
    print("="*60 + "\n")
    return True


def initializeSubmodules(project_root=None):
    """
    Initialize and update git submodules (for allolib dependency).
    Only initializes if not already done (idempotent).
    
    Parameters:
    -----------
    project_root : Path or str, optional
        Project root directory. If None, will use parent of this file's directory.
    
    Returns:
    --------
    bool
        True if submodules initialized successfully, False otherwise
    """
    if project_root is None:
        project_root = get_repo_root()
    else:
        project_root = Path(project_root).resolve()
    
    # Check if allolib submodule is already initialized
    allolib_path = project_root / "thirdparty" / "allolib"
    allolib_include = allolib_path / "include"
    
    if allolib_include.exists():
        print("✓ Git submodules already initialized")
        return True
    
    # Submodule not initialized, proceed with initialization
    try:
        print("Initializing git submodules (allolib, depth=1)...")
        result = subprocess.run(
            ["git", "submodule", "update", "--init", "--recursive", "--depth", "1"],
            cwd=str(project_root),
            check=True,
            capture_output=True,
            text=True
        )
        if result.stdout:
            print(result.stdout)
        print("✓ Git submodules initialized (shallow, depth=1)")
        return True
        
    except subprocess.CalledProcessError as e:
        print(f"\n✗ Submodule initialization failed with error code {e.returncode}")
        print(f"stdout: {e.stdout}")
        print(f"stderr: {e.stderr}")
        return False
    except Exception as e:
        print(f"\n✗ Unexpected error during submodule initialization: {e}")
        return False


def buildSpatialRenderer(build_dir="spatial_engine/spatialRender/build", source_dir="spatial_engine/spatialRender"):
    """
    Build the Spatial renderer using CMake.
    Only builds if executable doesn't exist (idempotent).
    
    Supports VBAP, DBAP, and LBAP spatializers from AlloLib.
    
    Parameters:
    -----------
    build_dir : str
        Build directory path (relative to project root)
    source_dir : str
        Source directory containing CMakeLists.txt (relative to project root)
    
    Returns:
    --------
    bool
        True if build succeeded or executable already exists, False otherwise
    """
    project_root = get_repo_root()
    build_path = project_root / build_dir
    executable = project_root / build_dir / "spatialroot_spatial_render"
    
    # Check if executable already exists
    if executable.exists():
        print(f"✓ Spatial renderer already built at: {executable}")
        return True
    
    # Executable doesn't exist, proceed with build
    print("Building Spatial renderer...")
    return runCmake(build_dir, source_dir)


# Backwards compatibility alias
def buildVBAPRenderer(build_dir="spatial_engine/spatialRender/build", source_dir="spatial_engine/spatialRender"):
    """
    DEPRECATED: Use buildSpatialRenderer() instead.
    This alias is kept for backwards compatibility.
    """
    print("Note: buildVBAPRenderer() is deprecated, use buildSpatialRenderer()")
    return buildSpatialRenderer(build_dir, source_dir)


def buildRealtimeEngine(build_dir="spatial_engine/realtimeEngine/build", source_dir="spatial_engine/realtimeEngine"):
    """
    Build the Realtime engine using CMake.
    Only builds if executable doesn't exist (idempotent).
    
    Supports real-time spatial audio rendering with DBAP.
    
    Parameters:
    -----------
    build_dir : str
        Build directory path (relative to project root)
    source_dir : str
        Source directory containing CMakeLists.txt (relative to project root)
    
    Returns:
    --------
    bool
        True if build succeeded or executable already exists, False otherwise
    """
    project_root = get_repo_root()
    build_path = project_root / build_dir
    executable = project_root / build_dir / "spatialroot_realtime"
    
    # Check if executable already exists
    if executable.exists():
        print(f"✓ Realtime engine already built at: {executable}")
        return True
    
    # Executable doesn't exist, proceed with build
    print("Building Realtime engine...")
    return runCmake(build_dir, source_dir)


def runCmake(build_dir="spatialRender/build", source_dir="spatialRender"):
    """
    Run CMake configuration and make to build the Spatial renderer.
    This is called by buildSpatialRenderer() and performs the actual build.
    
    Parameters:
    -----------
    build_dir : str
        Build directory path (relative to project root)
    source_dir : str
        Source directory containing CMakeLists.txt (relative to project root)
    
    Returns:
    --------
    bool
        True if build succeeded, False otherwise
    """
    project_root = get_repo_root()
    build_path = project_root / build_dir
    source_path = project_root / source_dir
    
    # Check if CMakeLists.txt exists
    cmake_file = source_path / "CMakeLists.txt"
    if not cmake_file.exists():
        print(f"✗ Error: CMakeLists.txt not found at {cmake_file}")
        return False
    
    # Create build directory if it doesn't exist
    build_path.mkdir(parents=True, exist_ok=True)
    
    print(f"  Source: {source_path}")
    print(f"  Build dir: {build_path}")
    
    try:
        # Ensure submodules are initialized before building
        if not initializeSubmodules(project_root):
            return False
        
        # Run CMake configuration
        print("\n  Running CMake configuration...")
        result = subprocess.run(
            ["cmake", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5", str(source_path)],
            cwd=str(build_path),
            check=True,
            capture_output=True,
            text=True
        )
        print(result.stdout)
        
        # Run make with parallel jobs for faster compilation
        print("\n  Running make (parallel build)...")
        
        # Use parallel build with number of CPU cores
        import multiprocessing
        num_cores = multiprocessing.cpu_count()
        print(f"  Using {num_cores} CPU cores for compilation...")
        
        result = subprocess.run(
            ["make", f"-j{num_cores}"],
            cwd=str(build_path),
            check=True,
            capture_output=True,
            text=True
        )
        print(result.stdout)
        
        print("✓ Spatial renderer build complete!")
        return True

    except subprocess.CalledProcessError as e:
        print(f"\n✗ Build failed with error code {e.returncode}")
        print(f"stdout: {e.stdout}")
        print(f"stderr: {e.stderr}")
        return False
    except FileNotFoundError:
        print("\n✗ Error: cmake or make not found. Please install CMake and build tools.")
        return False
    except Exception as e:
        print(f"\n✗ Unexpected error during build: {e}")
        return False


def initializeEbuSubmodules(project_root=None):
    """
    Initialize the EBU submodules (libbw64 and libadm) under thirdparty/.
    These are needed to build the embedded ADM extractor tool (Track A).
    Idempotent — skips if both directories already contain content.

    Parameters:
    -----------
    project_root : Path or str, optional
        Project root directory. Defaults to parent of this file's directory.

    Returns:
    --------
    bool
        True if submodules are present after this call, False on error.
    """
    if project_root is None:
        project_root = get_repo_root()
    else:
        project_root = Path(project_root).resolve()

    libbw64_path = project_root / "thirdparty" / "libbw64"
    libadm_path  = project_root / "thirdparty" / "libadm"

    # Consider initialized if both directories have content
    already_init = (
        libbw64_path.exists() and any(libbw64_path.iterdir()) and
        libadm_path.exists()  and any(libadm_path.iterdir())
    )

    if already_init:
        print("✓ EBU submodules (libbw64, libadm) already initialized")
        return True

    print("Initializing EBU submodules (libbw64, libadm)...")
    try:
        # Step 1: register the submodules into .git/config
        subprocess.run(
            ["git", "submodule", "init",
             "thirdparty/libbw64", "thirdparty/libadm"],
            cwd=str(project_root),
            check=True,
            capture_output=True,
            text=True
        )
        # Step 2: clone / update them (shallow depth=1 to reduce clone size)
        result = subprocess.run(
            ["git", "submodule", "update", "--depth", "1",
             "thirdparty/libbw64", "thirdparty/libadm"],
            cwd=str(project_root),
            check=True,
            capture_output=True,
            text=True
        )
        if result.stdout:
            print(result.stdout)
        print("✓ EBU submodules initialized")
        return True

    except subprocess.CalledProcessError as e:
        print(f"\n✗ EBU submodule initialization failed (exit {e.returncode})")
        print(f"stdout: {e.stdout}")
        print(f"stderr: {e.stderr}")
        return False
    except Exception as e:
        print(f"\n✗ Unexpected error initializing EBU submodules: {e}")
        return False


def buildAdmExtractor(
    build_dir="src/adm_extract/build",
    source_dir="src/adm_extract",
):
    """
    Build the embedded ADM extractor tool (spatialroot_adm_extract) using CMake.
    Extracts the axml chunk from BW64/RF64/WAV files using the EBU libbw64 library.
    Only builds if the executable is not already present (idempotent).

    The resulting binary is placed at:
        tools/adm_extract/build/spatialroot_adm_extract

    Parameters:
    -----------
    build_dir : str
        Build directory path (relative to project root).
    source_dir : str
        Directory containing the tool's CMakeLists.txt (relative to project root).

    Returns:
    --------
    bool
        True if the executable exists (or was built successfully), False otherwise.
    """
    project_root = get_repo_root()
    build_path   = project_root / build_dir
    source_path  = project_root / source_dir
    executable   = build_path / "spatialroot_adm_extract"

    if executable.exists():
        print(f"✓ ADM extractor already built at: {executable}")
        return True

    cmake_file = source_path / "CMakeLists.txt"
    if not cmake_file.exists():
        print(f"✗ ADM extractor source not found at {cmake_file}")
        print("  Run: git submodule update --init thirdparty/libbw64 thirdparty/libadm")
        print("  and ensure tools/adm_extract/ exists in the repo.")
        return False

    # EBU submodules must be present before building
    libbw64_include = project_root / "thirdparty" / "libbw64" / "include"
    if not libbw64_include.exists():
        print("✗ thirdparty/libbw64 not initialized — run initializeEbuSubmodules() first")
        return False

    build_path.mkdir(parents=True, exist_ok=True)

    print("Building embedded ADM extractor (spatialroot_adm_extract)...")
    print(f"  Source:    {source_path}")
    print(f"  Build dir: {build_path}")

    try:
        import multiprocessing
        num_cores = multiprocessing.cpu_count()

        # CMake configure
        print("\n  Running CMake configuration...")
        result = subprocess.run(
            ["cmake", str(source_path)],
            cwd=str(build_path),
            check=True,
            capture_output=True,
            text=True
        )
        if result.stdout:
            print(result.stdout)

        # Build
        print(f"\n  Running make ({num_cores} cores)...")
        result = subprocess.run(
            ["make", f"-j{num_cores}"],
            cwd=str(build_path),
            check=True,
            capture_output=True,
            text=True
        )
        if result.stdout:
            print(result.stdout)

        if executable.exists():
            print(f"✓ ADM extractor built successfully: {executable}")
            return True
        else:
            print("✗ Build completed but executable not found — check CMake target name")
            return False

    except subprocess.CalledProcessError as e:
        print(f"\n✗ ADM extractor build failed (exit {e.returncode})")
        print(f"stdout: {e.stdout}")
        print(f"stderr: {e.stderr}")
        return False
    except FileNotFoundError:
        print("\n✗ cmake or make not found — install CMake and build tools")
        return False
    except Exception as e:
        print(f"\n✗ Unexpected error building ADM extractor: {e}")
        return False

