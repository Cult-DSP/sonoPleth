import subprocess
from pathlib import Path


def setupCppTools():
    """
    Complete setup for C++ tools and dependencies.
    Orchestrates installation of bwfmetaedit, submodule initialization,
    Spatial renderer build, and the embedded ADM extractor build.
    Only performs actions that are needed (idempotent).

    Returns:
    --------
    bool
        True if all setup succeeded, False otherwise
    """
    print("\n" + "="*60)
    print("Setting up C++ tools and dependencies...")
    print("="*60)

    # Step 1: Install bwfmetaedit if needed (kept as fallback)
    if not installBwfmetaedit():
        print("\n⚠ Warning: bwfmetaedit installation failed — will use embedded extractor instead")

    # Step 2: Initialize git submodules (allolib) if needed
    if not initializeSubmodules():
        print("\n✗ Error: Failed to initialize allolib submodule")
        return False

    # Step 3: Initialize EBU submodules (libbw64 + libadm) if needed
    if not initializeEbuSubmodules():
        print("\n⚠ Warning: EBU submodule initialization failed — embedded ADM extractor will not be built")
    else:
        # Step 4: Build the embedded ADM extractor tool
        if not buildAdmExtractor():
            print("\n⚠ Warning: ADM extractor build failed — bwfmetaedit fallback will be used")

    # Step 5: Build Spatial renderer if needed
    if not buildSpatialRenderer():
        print("\n✗ Error: Failed to build Spatial renderer")
        return False

    print("\n" + "="*60)
    print("✓ C++ tools setup complete!")
    print("="*60 + "\n")
    return True


def installBwfmetaedit():
    """
    Install bwfmetaedit using Homebrew.
    
    Returns:
    --------
    bool
        True if installation succeeded or already installed, False otherwise
    """
    print("\nChecking for bwfmetaedit...")
    
    # Check if already installed
    try:
        result = subprocess.run(
            ["which", "bwfmetaedit"],
            check=True,
            capture_output=True,
            text=True
        )
        print(f"✓ bwfmetaedit already installed at: {result.stdout.strip()}")
        return True
    except subprocess.CalledProcessError:
        pass  # Not installed, proceed with installation
    
    # Check if brew is available
    try:
        subprocess.run(
            ["which", "brew"],
            check=True,
            capture_output=True,
            text=True
        )
    except subprocess.CalledProcessError:
        print("\n✗ Error: Homebrew not found!")
        print("  Install Homebrew from: https://brew.sh")
        print("  Or manually install bwfmetaedit from: https://mediaarea.net/BWFMetaEdit")
        return False
    
    # Install bwfmetaedit
    print("Installing bwfmetaedit via Homebrew...")
    try:
        result = subprocess.run(
            ["brew", "install", "bwfmetaedit"],
            check=True,
            capture_output=True,
            text=True
        )
        print(result.stdout)
        print("✓ bwfmetaedit installed successfully!")
        return True
    except subprocess.CalledProcessError as e:
        print(f"\n✗ Installation failed with error code {e.returncode}")
        print(f"stdout: {e.stdout}")
        print(f"stderr: {e.stderr}")
        print("\nTry installing manually:")
        print("  brew install bwfmetaedit")
        print("  or download from: https://mediaarea.net/BWFMetaEdit")
        return False
    except Exception as e:
        print(f"\n✗ Unexpected error during installation: {e}")
        return False


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
        project_root = Path(__file__).parent.parent.resolve()
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
        print("Initializing git submodules (allolib)...")
        result = subprocess.run(
            ["git", "submodule", "update", "--init", "--recursive"],
            cwd=str(project_root),
            check=True,
            capture_output=True,
            text=True
        )
        if result.stdout:
            print(result.stdout)
        print("✓ Git submodules initialized")
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
    project_root = Path(__file__).parent.parent.resolve()
    build_path = project_root / build_dir
    executable = project_root / build_dir / "sonoPleth_spatial_render"
    
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
    project_root = Path(__file__).parent.parent.resolve()
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
        project_root = Path(__file__).parent.parent.resolve()
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
        # Step 2: clone / update them
        result = subprocess.run(
            ["git", "submodule", "update",
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
    Build the embedded ADM extractor tool (sonopleth_adm_extract) using CMake.
    This tool replaces the bwfmetaedit dependency for extracting the axml chunk
    from BW64/RF64/WAV files.  Only builds if the executable is not already present
    (idempotent).

    The resulting binary is placed at:
        tools/adm_extract/build/sonopleth_adm_extract

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
    project_root = Path(__file__).parent.parent.resolve()
    build_path   = project_root / build_dir
    source_path  = project_root / source_dir
    executable   = build_path / "sonopleth_adm_extract"

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

    print("Building embedded ADM extractor (sonopleth_adm_extract)...")
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

