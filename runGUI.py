import tkinter as tk
from tkinter import filedialog, scrolledtext
import threading
import sys
import subprocess
import platform
from pathlib import Path


#having threading issues at the moment -- avoid for now



# add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent))

from src.analyzeADM.extractMetadata import extractMetaData
from src.analyzeADM.checkAudioChannels import exportAudioActivity, channelHasAudio
from src.packageADM.packageForRender import packageForRender
from src.createRender import runVBAPRender
from src.analyzeRender import analyzeRenderOutput


class PipelineGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("sonoPleth Pipeline")
        self.root.geometry("800x600")
        self.root.configure(bg='white')
        
        # default paths
        self.project_root = Path(__file__).parent.parent
        self.source_file = tk.StringVar(value=str(self.project_root / "sourceData/POE-ATMOS-FINAL.wav"))
        self.speaker_layout = tk.StringVar(value=str(self.project_root / "vbapRender/allosphere_layout.json"))
        self.create_analysis = tk.BooleanVar(value=True)
        self.is_running = False
        
        self.setup_ui()
    
    def setup_ui(self):
        # title
        title = tk.Label(
            self.root,
            text="sonoPleth ADM to Spatial Audio Pipeline",
            font=('SF Pro', 18),
            bg='white',
            fg='black'
        )
        title.pack(pady=20)
        
        # input section
        input_frame = tk.Frame(self.root, bg='white')
        input_frame.pack(fill='x', padx=40, pady=10)
        
        # source adm file
        self.create_file_input(
            input_frame,
            "Source ADM File:",
            self.source_file,
            self.browse_source_file,
            0
        )
        
        # speaker layout
        self.create_file_input(
            input_frame,
            "Speaker Layout:",
            self.speaker_layout,
            self.browse_speaker_layout,
            1
        )
        
        # checkbox for analysis
        check_frame = tk.Frame(input_frame, bg='white')
        check_frame.grid(row=2, column=0, columnspan=3, sticky='w', pady=10)
        
        tk.Checkbutton(
            check_frame,
            text="Create render analysis PDF",
            variable=self.create_analysis,
            bg='white',
            fg='black',
            activebackground='white',
            font=('SF Pro', 11)
        ).pack(anchor='w')
        
        # buttons frame
        button_frame = tk.Frame(self.root, bg='white')
        button_frame.pack(pady=20)
        
        self.run_button = tk.Button(
            button_frame,
            text="Run Pipeline",
            command=self.run_pipeline,
            bg='#007AFF',
            fg='white',
            font=('SF Pro', 13),
            relief='flat',
            padx=30,
            pady=8,
            cursor='hand2'
        )
        self.run_button.pack(side='left', padx=10)
        
        self.view_pdf_button = tk.Button(
            button_frame,
            text="View Render Analysis",
            command=self.view_render_analysis,
            bg='white',
            fg='#007AFF',
            font=('SF Pro', 13),
            relief='solid',
            borderwidth=1,
            padx=30,
            pady=8,
            cursor='hand2'
        )
        self.view_pdf_button.pack(side='left', padx=10)
        
        # output console
        console_label = tk.Label(
            self.root,
            text="Pipeline Output:",
            font=('SF Pro', 12),
            bg='white',
            fg='black'
        )
        console_label.pack(anchor='w', padx=40, pady=(10, 5))
        
        self.console = scrolledtext.ScrolledText(
            self.root,
            height=15,
            bg='#F5F5F7',
            fg='black',
            font=('SF Mono', 10),
            relief='flat',
            padx=10,
            pady=10
        )
        self.console.pack(fill='both', expand=True, padx=40, pady=(0, 20))
        
        # redirect print to console
        sys.stdout = TextRedirector(self.console, "stdout")
    
    def create_file_input(self, parent, label_text, var, browse_cmd, row):
        label = tk.Label(
            parent,
            text=label_text,
            font=('SF Pro', 11),
            bg='white',
            fg='black'
        )
        label.grid(row=row, column=0, sticky='w', pady=8)
        
        entry = tk.Entry(
            parent,
            textvariable=var,
            font=('SF Mono', 10),
            bg='#F5F5F7',
            fg='black',
            relief='flat'
        )
        entry.grid(row=row, column=1, sticky='ew', padx=10, pady=8)
        
        browse_btn = tk.Button(
            parent,
            text="Browse",
            command=browse_cmd,
            bg='white',
            fg='#007AFF',
            font=('SF Pro', 10),
            relief='flat',
            cursor='hand2'
        )
        browse_btn.grid(row=row, column=2, pady=8)
        
        parent.grid_columnconfigure(1, weight=1)
    
    def browse_source_file(self):
        filename = filedialog.askopenfilename(
            title="Select Source ADM WAV File",
            filetypes=[("WAV files", "*.wav"), ("All files", "*.*")]
        )
        if filename:
            self.source_file.set(filename)
    
    def browse_speaker_layout(self):
        filename = filedialog.askopenfilename(
            title="Select Speaker Layout JSON",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")]
        )
        if filename:
            self.speaker_layout.set(filename)
    
    def view_render_analysis(self):
        pdf_path = self.project_root / "processedData/spatial_render_analysis.pdf"
        
        if not pdf_path.exists():
            print("Render analysis PDF not found. Run the pipeline with analysis enabled first.")
            return
        
        # open PDF with default viewer based on platform
        try:
            if platform.system() == 'Darwin':  # macOS
                subprocess.run(['open', str(pdf_path)])
            elif platform.system() == 'Windows':
                subprocess.run(['start', str(pdf_path)], shell=True)
            else:  # Linux
                subprocess.run(['xdg-open', str(pdf_path)])
            print(f"Opening render analysis: {pdf_path}")
        except Exception as e:
            print(f"Error opening PDF: {e}")
    
    def run_pipeline(self):
        if self.is_running:
            print("Pipeline is already running")
            return
        
        self.is_running = True
        self.run_button.config(state='disabled', bg='#CCCCCC')
        self.console.delete(1.0, tk.END)
        
        # run in background thread but handle matplotlib on main thread
        thread = threading.Thread(target=self.execute_pipeline_wrapper)
        thread.daemon = True
        thread.start()
    
    def execute_pipeline_wrapper(self):
        """Wrapper to handle the analysis separately on main thread"""
        try:
            source_file = self.source_file.get()
            speaker_layout = self.speaker_layout.get()
            create_analysis = self.create_analysis.get()
            
            # run everything except analysis in background
            self.execute_pipeline_core(source_file, speaker_layout)
            
            # schedule analysis on main thread if needed
            if create_analysis:
                self.root.after(0, self.run_analysis_on_main_thread)
            else:
                self.finish_pipeline()
                
        except Exception as e:
            print(f"\nError: {e}")
            import traceback
            traceback.print_exc()
            self.finish_pipeline()
    
    def execute_pipeline_core(self, source_file, speaker_layout):
        """Core pipeline without matplotlib"""
        print("Starting sonoPleth pipeline...\n")
        
        print("Checking audio channels for content...")
        exportAudioActivity(source_file, output_path="processedData/containsAudio.json", threshold_db=-100)
        contains_audio_data = channelHasAudio(source_file, threshold_db=-100, printChannelUpdate=False)
        
        print("\nExtracting ADM metadata from WAV file...")
        extracted_metadata = extractMetaData(source_file, "processedData/currentMetaData.xml")
        
        if extracted_metadata:
            xml_path = extracted_metadata
            print(f"Using extracted XML metadata at {xml_path}")
        else:
            print("Using default XML metadata file")
            xml_path = "data/POE-ATMOS-FINAL-metadata.xml"
        
        print("\nParsing ADM metadata to LUSID scene...")
        from LUSID.src.xml_etree_parser import parse_adm_xml_to_lusid_scene
        lusid_scene = parse_adm_xml_to_lusid_scene(xml_path, contains_audio=contains_audio_data)
        lusid_scene.summary()
        
        print("\nPackaging audio for render...")
        packageForRender(source_file, lusid_scene, contains_audio_data, "processedData")
        
        print("\nRunning VBAP spatial renderer...")
        runVBAPRender(
            source_folder="processedData/stageForRender",
            render_instructions="processedData/stageForRender/scene.lusid.json",
            speaker_layout=speaker_layout,
            output_file="processedData/spatial_render.wav"
        )
    
    def run_analysis_on_main_thread(self):
        """Run matplotlib analysis on main thread (required for macOS)"""
        try:
            print("\nAnalyzing rendered spatial audio...")
            analyzeRenderOutput(
                render_file="processedData/spatial_render.wav",
                output_pdf="processedData/spatial_render_analysis.pdf"
            )
        except Exception as e:
            print(f"\nError in analysis: {e}")
            import traceback
            traceback.print_exc()
        finally:
            self.finish_pipeline()
    
    def finish_pipeline(self):
        """Clean up after pipeline completes"""
        print("\n" + "="*50)
        print("Pipeline completed")
        print("="*50)
        self.is_running = False
        self.run_button.config(state='normal', bg='#007AFF')


class TextRedirector:
    """Redirect text output to GUI console"""
    def __init__(self, widget, tag="stdout"):
        self.widget = widget
        self.tag = tag
    
    def write(self, text):
        self.widget.insert(tk.END, text)
        self.widget.see(tk.END)
    
    def flush(self):
        pass


def main():
    root = tk.Tk()
    app = PipelineGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
