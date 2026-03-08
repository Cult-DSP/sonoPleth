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

    if not buildCultTranscoder():
        print("\n✗ Error: Failed to build cult-transcoder")
        return False

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


def initializeCultTranscoderSubmodules(project_root=None) -> bool:
    """
    Initialize the libbw64 submodule nested inside cult_transcoder/thirdparty/libbw64.

    cult_transcoder owns its own copy of libbw64 (tracked via cult_transcoder/.gitmodules).
    CMakeLists.txt issues FATAL_ERROR if bw64.hpp is missing, so this must run before
    the CMake configure step. (AGENTS-CULT §8, DEV-PLAN Phase 3)

    Parameters:
    -----------
    project_root : Path or str, optional
        spatialroot root directory. Defaults to get_repo_root().

    Returns:
    --------
    bool
        True if libbw64 headers are present after this call, False on error.
    """
    if project_root is None:
        project_root = get_repo_root()
    else:
        project_root = Path(project_root).resolve()

    cult_dir = project_root / "cult_transcoder"
    libbw64_header = cult_dir / "thirdparty" / "libbw64" / "include" / "bw64" / "bw64.hpp"

    if libbw64_header.exists():
        print("✓ cult_transcoder/thirdparty/libbw64 already initialized")
        return True

    print("Initializing cult_transcoder/thirdparty/libbw64 submodule...")
    try:
        result = subprocess.run(
            ["git", "submodule", "update", "--init", "--depth", "1",
             "thirdparty/libbw64"],
            cwd=str(cult_dir),
            check=True,
            capture_output=True,
            text=True,
        )
        if result.stdout:
            print(result.stdout)
        if libbw64_header.exists():
            print("✓ cult_transcoder/thirdparty/libbw64 initialized")
            return True
        print("✗ libbw64 headers still missing after submodule update")
        return False

    except subprocess.CalledProcessError as e:
        print(f"\n✗ cult_transcoder/thirdparty/libbw64 init failed (exit {e.returncode})")
        print(f"stdout: {e.stdout}")
        print(f"stderr: {e.stderr}")
        return False
    except Exception as e:
        print(f"\n✗ Unexpected error initializing cult_transcoder/thirdparty/libbw64: {e}")
        return False


def buildCultTranscoder(
    build_dir="cult_transcoder/build",
    source_dir="cult_transcoder",
) -> bool:
    """
    Build the cult-transcoder CLI using CMake (Windows).

    Build process:
    1. Ensure cult_transcoder/thirdparty/libbw64 submodule is initialized.
       (CMakeLists.txt issues FATAL_ERROR if bw64.hpp is missing — AGENTS-CULT §8)
    2. cmake -B <build_dir> -DCMAKE_BUILD_TYPE=Release <source_dir>
       FetchContent (Catch2, pugixml) is resolved automatically at configure time.
    3. cmake --build <build_dir> --config Release

    Binary paths (AGENTS-CULT §9):
        Windows : cult_transcoder/build/cult-transcoder.exe (called via .bat wrapper)

    Parameters:
    -----------
    build_dir : str
        CMake build directory path, relative to project root.
    source_dir : str
        Directory containing cult_transcoder's CMakeLists.txt, relative to project root.

    Returns:
    --------
    bool
        True if the binary exists (or was built successfully), False otherwise.
    """
    project_root = get_repo_root()
    build_path = project_root / build_dir
    source_path = project_root / source_dir
    executable = build_path / "Release" / exe("cult-transcoder")
    # Visual Studio multi-config generators place the binary under build/Release/
    # Single-config (Ninja) places it directly under build/ — check both
    executable_flat = build_path / exe("cult-transcoder")

    if executable.exists() or executable_flat.exists():
        found = executable if executable.exists() else executable_flat
        print(f"✓ cult-transcoder already built at: {found}")
        return True

    cmake_file = source_path / "CMakeLists.txt"
    if not cmake_file.exists():
        print(f"✗ cult_transcoder CMakeLists.txt not found at {cmake_file}")
        print("  Ensure the cult_transcoder submodule is initialized:")
        print("  git submodule update --init cult_transcoder")
        return False

    # libbw64 must be present before cmake configure or it will FATAL_ERROR
    if not initializeCultTranscoderSubmodules(project_root):
        return False

    build_path.mkdir(parents=True, exist_ok=True)

    print("Building cult-transcoder (Windows).")
    print(f"  Source:    {source_path}")
    print(f"  Build dir: {build_path}")

    try:
        import multiprocessing
        num_cores = multiprocessing.cpu_count()

        # Configure — FetchContent (Catch2, pugixml) resolved here automatically
        print("\n  Running CMake configuration (Release)...")
        result = subprocess.run(
            ["cmake", "-B", str(build_path),
             "-DCMAKE_BUILD_TYPE=Release",
             str(source_path)],
            cwd=str(project_root),
            check=True,
            capture_output=True,
            text=True,
        )
        if result.stdout:
            print(result.stdout)

        # Build — --config Release for Visual Studio multi-config generators
        print(f"\n  Running cmake --build ({num_cores} cores, Release)...")
        result = subprocess.run(
            ["cmake", "--build", str(build_path),
             "--parallel", str(num_cores),
             "--config", "Release"],
            cwd=str(project_root),
            check=True,
            capture_output=True,
            text=True,
        )
        if result.stdout:
            print(result.stdout)

        found = executable if executable.exists() else executable_flat
        if found.exists():
            print(f"✓ cult-transcoder built successfully: {found}")
            return True

        print("✗ Build completed but cult-transcoder.exe not found — check CMake target name")
        return False

    except subprocess.CalledProcessError as e:
        print(f"\n✗ cult-transcoder build failed (exit {e.returncode})")
        print(f"stdout: {e.stdout}")
        print(f"stderr: {e.stderr}")
        return False
    except FileNotFoundError:
        print("\n✗ cmake not found — install CMake and build tools")
        return False
    except Exception as e:
        print(f"\n✗ Unexpected error building cult-transcoder: {e}")
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