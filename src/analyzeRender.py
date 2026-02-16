import numpy as np
import soundfile as sf
import matplotlib.pyplot as plt
from pathlib import Path
import os


def analyzeRenderOutput(
    render_file="processedData/spatial_render.wav",
    output_pdf="processedData/spatial_render_analysis.pdf"
):
    """
    Analyze the rendered output and create plots of dB levels over time.
    Creates batches of 10 channels per subplot.
    
    Supports both standard WAV and RF64 formats (RF64 is used automatically
    by the renderer when output exceeds the WAV 4 GB limit).
    
    Parameters:
    -----------
    render_file : str
        Path to the multichannel render WAV file (WAV or RF64)
    output_pdf : str
        Path to save the output PDF with plots
    
    Returns:
    --------
    bool
        True if analysis succeeded, False otherwise
    """
    project_root = Path(__file__).parent.parent.resolve()
    render_path = (project_root / render_file).resolve()
    output_path = (project_root / output_pdf).resolve()
    
    # Check if render file exists
    if not render_path.exists():
        print(f"Error: Render file not found: {render_path}")
        return False
    
    print(f"Loading render file: {render_path}")
    
    # Use sf.info() first to get metadata without loading all audio into memory.
    # This correctly reads both WAV and RF64 headers.
    file_info = sf.info(str(render_path))
    num_channels = file_info.channels
    sr = file_info.samplerate
    num_samples = file_info.frames
    duration = file_info.duration
    file_format = file_info.format
    file_subtype = file_info.subtype
    
    # Sanity check: cross-check file size against reported sample count.
    # This catches corrupted WAV headers from pre-RF64 renders where the
    # 32-bit data-chunk size wrapped around at 4 GB.
    file_size_bytes = os.path.getsize(str(render_path))
    bytes_per_sample = 4 if "FLOAT" in file_subtype.upper() else 2  # float32 vs int16
    expected_data_bytes = num_samples * num_channels * bytes_per_sample
    # Allow some slack for headers (~100 KB is generous)
    if file_size_bytes > expected_data_bytes + 1_000_000:
        actual_data_bytes = file_size_bytes - 128  # approximate header size
        actual_samples = actual_data_bytes // (num_channels * bytes_per_sample)
        actual_duration = actual_samples / sr
        print(f"\n⚠ WARNING: File size ({file_size_bytes / (1024**3):.2f} GB) is much larger")
        print(f"  than header-reported data ({expected_data_bytes / (1024**3):.2f} GB).")
        print(f"  Header says {duration:.2f}s but file size suggests ~{actual_duration:.2f}s.")
        print(f"  This likely indicates a standard WAV file that exceeded the 4 GB limit.")
        print(f"  Re-render to fix (renderer now auto-selects RF64 for large files).\n")
    
    print(f"Format: {file_format} / {file_subtype}")
    print(f"Channels: {num_channels}")
    print(f"Sample rate: {sr} Hz")
    print(f"Duration: {duration:.2f} seconds")
    print(f"Samples: {num_samples}")
    
    # Load the audio file
    audio, sr = sf.read(str(render_path))
    
    # Calculate dB at 1-second intervals
    samples_per_second = sr
    num_time_points = int(np.floor(duration))
    
    print(f"\nCalculating dB levels at {num_time_points} time points...")
    
    # Store dB values for each channel
    db_values = np.zeros((num_channels, num_time_points))
    
    for ch in range(num_channels):
        for t in range(num_time_points):
            start_sample = t * samples_per_second
            end_sample = min(start_sample + samples_per_second, num_samples)
            
            # Get RMS for this second
            chunk = audio[start_sample:end_sample, ch]
            rms = np.sqrt(np.mean(chunk ** 2))
            
            # Convert to dB (reference: 1.0 = 0 dB)
            if rms > 0:
                db = 20 * np.log10(rms)
            else:
                db = -120  # Floor for silence
            
            db_values[ch, t] = db
    
    print("Creating plots...")
    
    # Create subplots in batches of 10 channels
    channels_per_plot = 10
    num_plots = int(np.ceil(num_channels / channels_per_plot))
    
    # Create figure with subplots
    fig, axes = plt.subplots(num_plots, 1, figsize=(12, 4 * num_plots))
    if num_plots == 1:
        axes = [axes]
    
    time_axis = np.arange(num_time_points)
    colors = plt.cm.tab10(np.linspace(0, 1, 10))
    
    for plot_idx in range(num_plots):
        ax = axes[plot_idx]
        
        start_ch = plot_idx * channels_per_plot
        end_ch = min(start_ch + channels_per_plot, num_channels)
        
        for i, ch in enumerate(range(start_ch, end_ch)):
            color_idx = i % 10
            ax.plot(time_axis, db_values[ch, :], 
                   label=f'Ch {ch + 1}', 
                   color=colors[color_idx],
                   alpha=0.7,
                   linewidth=1.5)
        
        ax.set_xlabel('Time (seconds)', fontsize=10)
        ax.set_ylabel('dB', fontsize=10)
        ax.set_title(f'Channels {start_ch + 1} - {end_ch}', fontsize=12, fontweight='bold')
        ax.grid(True, alpha=0.3)
        ax.legend(loc='upper right', fontsize=8, ncol=2)
        ax.set_ylim([-120, 0])
    
    plt.tight_layout()
    
    # Save to PDF
    print(f"Saving plot to: {output_path}")
    plt.savefig(str(output_path), dpi=150, bbox_inches='tight')
    plt.close()
    
    print(f"\n✓ Analysis complete! Plot saved to: {output_path}")
    
    # Print summary statistics
    print("\nSummary Statistics:")
    print(f"  Max dB across all channels: {np.max(db_values):.2f} dB")
    print(f"  Min dB across all channels: {np.min(db_values):.2f} dB")
    print(f"  Mean dB across all channels: {np.mean(db_values):.2f} dB")
    
    return True


if __name__ == "__main__":
    success = analyzeRenderOutput()
    if not success:
        print("\nAnalysis failed!")
