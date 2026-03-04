import subprocess
from pathlib import Path


def get_repo_root() -> Path:
    # __file__ = repo/src/config/configCPP_windows.py
    return Path(__file__).resolve().parents[2]


def exe(name: str) -> str:
    return f"{name}.exe"


def setupCppTools() -> bool:
    print("\n" + "=" * 60)
    print("Setting up C++ tools and dependencies (Windows)...")
    print("=" * 60)

    if not initializeSubmodules():
        print("\n✗ Error: Failed to initialize allolib submodule")
        return False

    # [SUPERSEDED — Phase 3 — 2026-03-04] Initialize EBU submodules (libbw64 + libadm)
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

    # [SUPERSEDED — Phase 3 — 2026-03-04] Build the embedded ADM extractor tool
    #
    # spatialroot_adm_extract is replaced by cult-transcoder --in-format adm_wav.
    # The src/adm_extract/ CMake project is deprecated and should not be built.
    # runRealtime.py now calls cult_transcoder/build/cult-transcoder directly.
    #
    # if not buildAdmExtractor():
    #     print("\n✗ Error: ADM extractor build failed")
    #     return False

    if not buildSpatialRenderer():
        print("\n✗ Error: Failed to build Spatial renderer")
        return False

    if not buildRealtimeEngine():
        print("\n✗ Error: Failed to build Realtime engine")
        return False

    print("\n" + "=" * 60)
    print("✓ C++ tools setup complete!")
    print("=" * 60 + "\n")
    return True


def initializeSubmodules(project_root=None) -> bool:
    if project_root is None:
        project_root = get_repo_root()
    else:
        project_root = Path(project_root).resolve()

    allolib_path = project_root / "thirdparty" / "allolib"
    allolib_include = allolib_path / "include"

    if allolib_include.exists():
        print("✓ Git submodules already initialized")
        return True

    try:
        print("Initializing git submodules (allolib, depth=1).")
        result = subprocess.run(
            ["git", "submodule", "update", "--init", "--recursive", "--depth", "1"],
            cwd=str(project_root),
            check=True,
            capture_output=True,
            text=True,
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


def buildSpatialRenderer(
    build_dir="spatial_engine/spatialRender/build",
    source_dir="spatial_engine/spatialRender",
) -> bool:
    project_root = get_repo_root()
    build_path = project_root / build_dir
    executable = build_path / exe("spatialroot_spatial_render")

    if executable.exists():
        print(f"✓ Spatial renderer already built at: {executable}")
        return True

    print("Building Spatial renderer.")
    return runCmake(build_dir, source_dir)


def buildRealtimeEngine(
    build_dir="spatial_engine/realtimeEngine/build",
    source_dir="spatial_engine/realtimeEngine",
) -> bool:
    project_root = get_repo_root()
    build_path = project_root / build_dir
    executable = build_path / exe("spatialroot_realtime")

    if executable.exists():
        print(f"✓ Realtime engine already built at: {executable}")
        return True

    print("Building Realtime engine.")
    return runCmake(build_dir, source_dir)


def runCmake(build_dir="spatialRender/build", source_dir="spatialRender") -> bool:
    project_root = get_repo_root()
    build_path = project_root / build_dir
    source_path = project_root / source_dir

    cmake_file = source_path / "CMakeLists.txt"
    if not cmake_file.exists():
        print(f"✗ Error: CMakeLists.txt not found at {cmake_file}")
        return False

    build_path.mkdir(parents=True, exist_ok=True)

    print(f"  Source: {source_path}")
    print(f"  Build dir: {build_path}")

    try:
        if not initializeSubmodules(project_root):
            return False

        print("\n  Running CMake configuration.")
        result = subprocess.run(
            ["cmake", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5", str(source_path)],
            cwd=str(build_path),
            check=True,
            capture_output=True,
            text=True,
        )
        if result.stdout:
            print(result.stdout)

        import multiprocessing
        num_cores = multiprocessing.cpu_count()
        print(f"\n  Running cmake --build ({num_cores} cores)...")

        # Visual Studio generators are multi-config -> include --config Release.
        result = subprocess.run(
            ["cmake", "--build", ".", "--parallel", str(num_cores), "--config", "Release"],
            cwd=str(build_path),
            check=True,
            capture_output=True,
            text=True,
        )
        if result.stdout:
            print(result.stdout)

        print("✓ Spatial renderer build complete!")
        return True

    except subprocess.CalledProcessError as e:
        print(f"\n✗ Build failed with error code {e.returncode}")
        print(f"stdout: {e.stdout}")
        print(f"stderr: {e.stderr}")
        return False
    except FileNotFoundError:
        print("\n✗ Error: cmake not found. Please install CMake and build tools.")
        return False
    except Exception as e:
        print(f"\n✗ Unexpected error during build: {e}")
        return False


def initializeEbuSubmodules(project_root=None) -> bool:
    if project_root is None:
        project_root = get_repo_root()
    else:
        project_root = Path(project_root).resolve()

    libbw64_path = project_root / "thirdparty" / "libbw64"
    libadm_path = project_root / "thirdparty" / "libadm"

    already_init = (
        libbw64_path.exists()
        and any(libbw64_path.iterdir())
        and libadm_path.exists()
        and any(libadm_path.iterdir())
    )

    if already_init:
        print("✓ EBU submodules (libbw64, libadm) already initialized")
        return True

    print("Initializing EBU submodules (libbw64, libadm).")
    try:
        subprocess.run(
            ["git", "submodule", "init", "thirdparty/libbw64", "thirdparty/libadm"],
            cwd=str(project_root),
            check=True,
            capture_output=True,
            text=True,
        )
        result = subprocess.run(
            ["git", "submodule", "update", "--depth", "1", "thirdparty/libbw64", "thirdparty/libadm"],
            cwd=str(project_root),
            check=True,
            capture_output=True,
            text=True,
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


def buildAdmExtractor(build_dir="src/adm_extract/build", source_dir="src/adm_extract") -> bool:
    project_root = get_repo_root()
    build_path = project_root / build_dir
    source_path = project_root / source_dir
    executable = build_path / exe("spatialroot_adm_extract")

    if executable.exists():
        print(f"✓ ADM extractor already built at: {executable}")
        return True

    cmake_file = source_path / "CMakeLists.txt"
    if not cmake_file.exists():
        print(f"✗ ADM extractor source not found at {cmake_file}")
        print("  Run: git submodule update --init thirdparty/libbw64 thirdparty/libadm")
        return False

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

        print("\n  Running CMake configuration...")
        result = subprocess.run(
            ["cmake", str(source_path)],
            cwd=str(build_path),
            check=True,
            capture_output=True,
            text=True,
        )
        if result.stdout:
            print(result.stdout)

        print(f"\n  Running cmake --build ({num_cores} cores)...")
        result = subprocess.run(
            ["cmake", "--build", ".", "--parallel", str(num_cores), "--config", "Release"],
            cwd=str(build_path),
            check=True,
            capture_output=True,
            text=True,
        )
        if result.stdout:
            print(result.stdout)

        if executable.exists():
            print(f"✓ ADM extractor built successfully: {executable}")
            return True

        print("✗ Build completed but executable not found — check CMake target name")
        return False

    except subprocess.CalledProcessError as e:
        print(f"\n✗ ADM extractor build failed (exit {e.returncode})")
        print(f"stdout: {e.stdout}")
        print(f"stderr: {e.stderr}")
        return False
    except FileNotFoundError:
        print("\n✗ cmake not found — install CMake and build tools")
        return False
    except Exception as e:
        print(f"\n✗ Unexpected error building ADM extractor: {e}")
        return False