import subprocess
from pathlib import Path


def setupCppTools():
    """
    Complete setup for C++ tools and dependencies.
    Orchestrates installation of bwfmetaedit, submodule initialization, and VBAP renderer build.
    Only performs actions that are needed (idempotent).
    
    Returns:
    --------
    bool
        True if all setup succeeded, False otherwise
    """
    print("\n" + "="*60)
    print("Setting up C++ tools and dependencies...")
    print("="*60)
    
    # Step 1: Install bwfmetaedit if needed
    if not installBwfmetaedit():
        print("\n⚠ Warning: bwfmetaedit installation failed, but continuing...")
    
    # Step 2: Initialize git submodules (allolib) if needed
    if not initializeSubmodules():
        print("\n✗ Error: Failed to initialize submodules")
        return False
    
    # Step 3: Build VBAP renderer if needed
    if not buildVBAPRenderer():
        print("\n✗ Error: Failed to build VBAP renderer")
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


def buildVBAPRenderer(build_dir="spatial_engine/vbapRender/build", source_dir="spatial_engine/vbapRender"):
    """
    Build the VBAP renderer using CMake.
    Only builds if executable doesn't exist (idempotent).
    
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
    executable = project_root / build_dir / "sonoPleth_vbap_render"
    
    # Check if executable already exists
    if executable.exists():
        print(f"✓ VBAP renderer already built at: {executable}")
        return True
    
    # Executable doesn't exist, proceed with build
    print("Building VBAP renderer...")
    return runCmake(build_dir, source_dir)


def runCmake(build_dir="vbapRender/build", source_dir="vbapRender"):
    """
    Run CMake configuration and make to build the VBAP renderer.
    This is called by buildVBAPRenderer() and performs the actual build.
    
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
        
        print("✓ VBAP renderer build complete!")
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

