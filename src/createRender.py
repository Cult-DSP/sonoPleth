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


def runVBAPRender(
    source_folder="processedData/stageForRender",
    render_instructions="processedData/stageForRender/renderInstructions.json",
    speaker_layout="spatial_engine/speaker_layouts/allosphere_layout.json",
    output_file="processedData/spatial_render.wav"
):
    """
    
    params:
    -----------
    source_folder : str
        Directory containing mono source WAV files (src_*.wav)
    render_instructions : str
        JSON file with spatial position data
    speaker_layout : str
        JSON file with speaker configuration
    output_file : str
        Output multichannel WAV file path
    
    Returns:
    --------
    bool
        True if render succeeded, False otherwise
    """
    # Get absolute paths
    project_root = Path(__file__).parent.parent.resolve()

    deleteRenderOutput(output_file)
    executable = project_root / "spatial_engine" / "vbapRender" / "build" / "sonoPleth_vbap_render"
    
    # Check if executable exists
    if not executable.exists():
        print(f"Error: Executable not found at {executable}")
        print("Run setupCppTools() from src.configCPP to build the renderer")
        return False
    
    # Make paths absolute
    source_folder = str((project_root / source_folder).resolve())
    render_instructions = str((project_root / render_instructions).resolve())
    speaker_layout = str((project_root / speaker_layout).resolve())
    output_file = str((project_root / output_file).resolve())
    
    # Check if inputs exist
    if not Path(source_folder).exists():
        print(f"Error: Source folder not found: {source_folder}")
        return False
    if not Path(render_instructions).exists():
        print(f"Error: Render instructions not found: {render_instructions}")
        return False
    if not Path(speaker_layout).exists():
        print(f"Error: Speaker layout not found: {speaker_layout}")
        return False
    
    # Run the renderer
    print(f"\nRunning VBAP Renderer...")
    print(f"  Source folder: {source_folder}")
    print(f"  Instructions: {render_instructions}")
    print(f"  Speaker layout: {speaker_layout}")
    print(f"  Output: {output_file}\n")
    
    try:
        result = subprocess.run(
            [
                str(executable),
                "--layout", speaker_layout,
                "--positions", render_instructions,
                "--sources", source_folder,
                "--out", output_file
            ],
            check=True,
            capture_output=False,
            text=True
        )
        
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
    except Exception as e:
        print(f"\n✗ Unexpected error: {e}")
        return False


if __name__ == "__main__":
    # Delete old render if it exists
    
    # Run the render
    success = runVBAPRender()
    if success:
        print("\nVBAP render completed successfully!")
    else:
        print("\nVBAP render failed!")