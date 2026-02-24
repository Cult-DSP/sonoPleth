#ifndef MAINPLAYER_HPP
#define MAINPLAYER_HPP

/*
54-Channel Audio Playback System
Plays back a multichannel audio file with all channels mapped to individual outputs.
Includes GUI controls for playback, pause, loop, and rewind.
Includes real-time dB meters for all 54 channels.
*/

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>
#include "al/app/al_App.hpp"
#include "al/io/al_File.hpp"
#include "al/io/al_Imgui.hpp"
#include "Gamma/SoundFile.h"
#include "channelMapping.hpp"

using namespace al;

// Buffer states for double buffering system
enum BufferState { EMPTY, LOADING, READY, PLAYING };

struct adm_player {
  gam::SoundFile soundFile;
  std::mutex soundFileMutex;  // Mutex for thread-safe access to soundFile
  std::atomic<uint64_t> frameCounter = {0};
  std::vector<float> buffer;

  // Playback controls
  bool playing = false;
  bool loop = true;
  float gain = 0.5f;
  bool streamingMode = true;  // Enable streaming for large files
  uint64_t chunkSize = 60 * 48000;  // 1 minute chunks at 48kHz
  
  // Double buffering system
  std::vector<float> bufferA, bufferB;
  std::atomic<BufferState> stateA, stateB;
  std::atomic<uint64_t> chunkStartA, chunkStartB;
  std::atomic<int> activeBufferIndex;  // 0 = A, 1 = B
  
  // Background loading thread
  std::thread loaderThread;
  std::atomic<bool> loaderRunning;
  std::atomic<uint64_t> loadRequestChunk;
  std::atomic<bool> loadRequested;
  
  // Legacy streaming variables (for backward compatibility)
  std::vector<float> audioData;  // Chunked audio data
  uint64_t currentChunkStart = 0;
  uint64_t currentChunkFrames = 0;

  // Audio file info
  int numChannels = 56; //default 
  int expectedChannels = 60; //default
  std::string audioFolder;
  // std::string audioFolder = "../adm-allo-player/sourceAudio/";
  //std::string audioFileName = "1-swale-allo-render.wav";
  // selection is done via audioFiles + selectedFileIndex (no single audioFileName string)

  // Metering
  std::vector<float> channelLevels;  // Linear amplitude for each channel
  std::vector<float> channelPeaks;   // Peak hold for each channel
  int peakHoldFrames = 24;           // How long to hold peaks (in render frames)
  std::vector<int> peakHoldCounters; // Counter for peak hold
  float meterDecayRate = 0.95f;      // How fast meters decay
  bool showMeters = true;

  // File selection
  std::vector<std::string> audioFiles;  // List of available audio files
  int selectedFileIndex = 0;            // Currently selected file index

  //gui 
  bool displayGUI;
  public:
  void toggleGUI(bool toggle = false) {
    displayGUI = toggle;
  }

  void setSourceAudioFolder(const std::string& folder) {
    audioFolder = folder;
  }
  void scanAudioFiles() {
    audioFiles.clear();
    std::string audioDir = al::File::currentPath() + audioFolder;

    std::cout << "Scanning for audio files in: " << audioDir << std::endl;

    try {
      // Use al::filterInDir to find .wav files
      al::FileList wavFiles = al::filterInDir(audioDir, [](const al::FilePath& fp) {
        return al::checkExtension(fp, ".wav");
      }, false); // false = not recursive

      // Convert FileList to vector of strings
      for (auto& fp : wavFiles) {
        audioFiles.push_back(fp.file());
      }

      // Make ordering deterministic: lexicographic sort (case-sensitive, std::string <)
      std::sort(audioFiles.begin(), audioFiles.end());
    } catch (const std::exception& e) {
      std::cerr << "Error scanning audio directory: " << e.what() << std::endl;
    }

    std::cout << "Found " << audioFiles.size() << " audio files" << std::endl;
  }

  // Load a new audio file
  bool loadAudioFile(const std::string& filename) {
    std::string audioPath = al::File::currentPath() + audioFolder + filename;

    std::cout << "\n=== Loading new audio file ===" << std::endl;
    std::cout << "File: " << audioPath << std::endl;

    // Stop playback during load
    bool wasPlaying = playing;
    playing = false;

    if (!soundFile.openRead(audioPath)) {
      std::cerr << "✗ ERROR: Could not open file: " << audioPath << std::endl;
      return false;
    }

    std::cout << "✓ Audio file loaded successfully" << std::endl;
    std::cout << "  Sample rate: " << soundFile.frameRate() << " Hz" << std::endl;
    std::cout << "  Channels: " << soundFile.channels() << std::endl;
    std::cout << "  Frame count: " << soundFile.frames() << std::endl;
    std::cout << "  Duration: " << (double)soundFile.frames() / soundFile.frameRate() << " seconds" << std::endl;

    // For streaming mode, we don't preload data - Gamma SoundFile doesn't load data by default
    if (streamingMode) {
      std::cout << "  Streaming mode enabled - data not loaded into memory" << std::endl;
    }
    // note: we don't store a single filename string; selection is tracked by audioFiles[selectedFileIndex]

    if (numChannels != expectedChannels) {
      std::cerr << "⚠ WARNING: Expected " << expectedChannels << " channels but file has "
                << numChannels << " channels." << std::endl;
    }

    // For streaming mode, load first chunk and initialize double buffering
    if (streamingMode) {
      // Invalidate all buffers when loading new file
      stateA.store(EMPTY);
      stateB.store(EMPTY);
      activeBufferIndex.store(-1);
      
      // Load first chunk into buffer A synchronously
      loadChunkIntoBuffer(0, bufferA, stateA, chunkStartA);
      
      // Verify the buffer was loaded correctly
      if (stateA.load() == READY && bufferA.size() > 0) {
        activeBufferIndex.store(0);
        stateA.store(PLAYING);
        std::cout << "  Successfully initialized buffer A with " << bufferA.size() << " samples" << std::endl;
      } else {
        std::cerr << "  ERROR: Failed to load initial buffer!" << std::endl;
        streamingMode = false;  // Fall back to non-streaming
      }
      
      std::cout << "  Streaming mode enabled - loaded first chunk into double buffer" << std::endl;
    }
    // note: we don't store a single filename string; selection is tracked by audioFiles[selectedFileIndex]

    if (numChannels != expectedChannels) {
      std::cerr << "⚠ WARNING: Expected " << expectedChannels << " channels but file has "
                << numChannels << " channels." << std::endl;
    }

    // Reset playback position
    frameCounter.store(0);

    // Resize buffers for new channel count
    int framesPerBuffer = 512;
    buffer.resize(framesPerBuffer * numChannels);
    channelLevels.resize(expectedChannels, 0.0f);
    channelPeaks.resize(expectedChannels, 0.0f);
    peakHoldCounters.resize(expectedChannels, 0);

    // Resume playback if was playing
    playing = wasPlaying;

    return true;
  }

  void loadAudioChunk(uint64_t chunkStartFrame) {
    if (!streamingMode) return;

    // Calculate chunk size in frames (1 minute chunks)
    uint64_t chunkFrames = chunkSize;

    // Ensure we don't read beyond file end
    if (chunkStartFrame + chunkFrames > soundFile.frames()) {
      chunkFrames = soundFile.frames() - chunkStartFrame;
    }

    // Resize audioData to hold the chunk
    audioData.resize(chunkFrames * numChannels);

    // Seek to the correct position in the file
    soundFile.seek(chunkStartFrame, SEEK_SET);

    // Read the chunk data
    soundFile.read(&audioData[0], chunkFrames);

    // Update current chunk info
    currentChunkStart = chunkStartFrame;
    currentChunkFrames = chunkFrames;

    std::cout << "Loaded chunk: frames " << chunkStartFrame << " to " << (chunkStartFrame + chunkFrames - 1)
              << " (" << chunkFrames << " frames)" << std::endl;
  }

  // Double buffering methods
  void initializeDoubleBuffering() {
    // Pre-allocate buffers
    bufferA.reserve(chunkSize * numChannels);
    bufferB.reserve(chunkSize * numChannels);
    
    // Initialize states
    stateA.store(EMPTY);
    stateB.store(EMPTY);
    chunkStartA.store(-1);
    chunkStartB.store(-1);
    activeBufferIndex.store(-1);  // No active buffer initially
    
    // Start loader thread
    loaderRunning.store(true);
    loaderThread = std::thread([this]() { loaderWorker(); });
  }
  
  void cleanupDoubleBuffering() {
    loaderRunning.store(false);
    if (loaderThread.joinable()) {
      loaderThread.join();
    }
  }
  
  void loaderWorker() {
    while (loaderRunning.load()) {
      if (loadRequested.load()) {
        loadRequested.store(false);
        uint64_t chunkToLoad = loadRequestChunk.load();
        loadChunkIntoInactiveBuffer(chunkToLoad);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  
  void loadChunkIntoInactiveBuffer(uint64_t chunkStart) {
    // Find an empty buffer to load into
    if (stateA.load() == EMPTY) {
      loadChunkIntoBuffer(chunkStart, bufferA, stateA, chunkStartA);
    } else if (stateB.load() == EMPTY) {
      loadChunkIntoBuffer(chunkStart, bufferB, stateB, chunkStartB);
    } else {
      // Both buffers busy - evict the non-playing one
      if (activeBufferIndex.load() == 0) {
        loadChunkIntoBuffer(chunkStart, bufferB, stateB, chunkStartB);
      } else {
        loadChunkIntoBuffer(chunkStart, bufferA, stateA, chunkStartA);
      }
    }
  }
  
  void loadChunkIntoBuffer(uint64_t chunkStart, std::vector<float>& targetBuffer, 
                          std::atomic<BufferState>& state, std::atomic<uint64_t>& chunkStartVar) {
    state.store(LOADING);
    
    try {
      uint64_t actualChunkSize = getChunkSize(chunkStart);
      targetBuffer.resize(actualChunkSize * numChannels);
      
      uint64_t framesRead = 0;
      {
        std::lock_guard<std::mutex> lock(soundFileMutex);
        soundFile.seek(chunkStart, SEEK_SET);
        
        uint64_t readBlockSize = 512;
        while (framesRead < actualChunkSize) {
          uint64_t framesToRead = std::min(actualChunkSize - framesRead, readBlockSize);
          soundFile.read(&targetBuffer[framesRead * numChannels], framesToRead);
          uint64_t actuallyRead = soundFile.frames();
          framesRead += actuallyRead;
          if (actuallyRead < framesToRead) break;  // End of file
        }
        
        // Fill remaining with silence
        std::fill(targetBuffer.begin() + framesRead * numChannels, targetBuffer.end(), 0.0f);
      }
      
      if (framesRead == actualChunkSize) {
        chunkStartVar.store(chunkStart);
        state.store(READY);
        std::cout << "Loaded chunk: frames " << chunkStart << " to " << (chunkStart + actualChunkSize - 1)
                  << " (" << actualChunkSize << " frames) into buffer" << std::endl;
      } else {
        state.store(EMPTY);
        std::cerr << "Warning: Failed to load chunk at " << chunkStart << ", read " << framesRead << " of " << actualChunkSize << " frames" << std::endl;
      }
    } catch (const std::exception& e) {
      state.store(EMPTY);
      std::cerr << "Error loading chunk: " << e.what() << std::endl;
    }
  }
  
  uint64_t getChunkSize(uint64_t chunkStart) {
    uint64_t remaining = soundFile.frames() - chunkStart;
    return std::min(chunkSize, remaining);
  }
  
  uint64_t getNextChunkStart(uint64_t currentChunkStart) {
    uint64_t nextStart = currentChunkStart + chunkSize;
    
    if (nextStart >= soundFile.frames()) {
      if (loop) {
        return 0;  // Loop back to beginning
      } else {
        return UINT64_MAX;  // No more chunks
      }
    }
    return nextStart;
  }
  
  void requestLoadIntoInactiveBuffer(uint64_t chunkStart) {
    // Only request if not already loaded/requested
    if (!isChunkLoadedInAnyBuffer(chunkStart)) {
      loadRequestChunk.store(chunkStart);
      loadRequested.store(true);
    }
  }
  
  bool isChunkLoadedInAnyBuffer(uint64_t chunkStart) {
    return (stateA.load() == READY && chunkStartA.load() == chunkStart) ||
           (stateB.load() == READY && chunkStartB.load() == chunkStart);
  }
  
  bool trySwitchToBufferWithChunk(uint64_t chunkStart) {
    std::cout << "Trying to switch to chunk " << chunkStart << std::endl;
    std::cout << "Buffer A: state=" << stateA.load() << ", chunkStart=" << chunkStartA.load() 
              << ", size=" << bufferA.size() << std::endl;
    std::cout << "Buffer B: state=" << stateB.load() << ", chunkStart=" << chunkStartB.load() 
              << ", size=" << bufferB.size() << std::endl;
    
    if (stateA.load() == READY && chunkStartA.load() == chunkStart) {
      // Additional check: ensure buffer has data
      if (bufferA.size() > 0) {
        // Mark old buffer as empty before switching
        int oldActive = activeBufferIndex.load();
        if (oldActive == 1) stateB.store(EMPTY);
        
        activeBufferIndex.store(0);
        stateA.store(PLAYING);
        std::cout << "Switched to buffer A" << std::endl;
        return true;
      }
    } else if (stateB.load() == READY && chunkStartB.load() == chunkStart) {
      // Additional check: ensure buffer has data
      if (bufferB.size() > 0) {
        // Mark old buffer as empty before switching
        int oldActive = activeBufferIndex.load();
        if (oldActive == 0) stateA.store(EMPTY);
        
        activeBufferIndex.store(1);
        stateB.store(PLAYING);
        std::cout << "Switched to buffer B" << std::endl;
        return true;
      }
    }
    std::cout << "No suitable buffer found or buffer empty" << std::endl;
    return false;
  }
  
  std::vector<float>& getActiveBuffer() {
    return (activeBufferIndex.load() == 0) ? bufferA : bufferB;
  }
  
  uint64_t getActiveBufferChunkStart() {
    if (activeBufferIndex.load() == 0) {
      return chunkStartA.load();
    } else if (activeBufferIndex.load() == 1) {
      return chunkStartB.load();
    }
    return 0;
  }
  
  void performDirectRead(uint64_t chunkStart, uint64_t numFrames) {
    // Temporary direct read (slower but prevents dropout)
    uint64_t startFrame = chunkStart;
    uint64_t maxFrame = soundFile.frames();
    uint64_t endFrame = std::min(startFrame + numFrames, maxFrame);
    uint64_t actualFrames = endFrame - startFrame;
    
    int framesRead = 0;
    {
      std::lock_guard<std::mutex> lock(soundFileMutex);
      soundFile.seek(startFrame, SEEK_SET);
      framesRead = soundFile.read(buffer.data(), actualFrames);
    }
    
    std::cout << "Direct read: requested " << numFrames << " frames from " << chunkStart 
              << ", read " << framesRead << " frames" << std::endl;
    
    // If we read fewer frames than requested, fill the rest with silence
    if (framesRead < (int)numFrames) {
      std::fill(buffer.begin() + framesRead * numChannels, buffer.end(), 0.0f);
    }
  }

  void onAnimate(double dt) {
    if (!soundFile.opened() || !streamingMode) return;
    
    uint64_t currentFrame = frameCounter.load();
    uint64_t currentChunk = (currentFrame / chunkSize) * chunkSize;
    float progressThroughChunk = (float)(currentFrame % chunkSize) / chunkSize;
    
    // Trigger loading at 50% through current chunk
    if (progressThroughChunk > 0.5f) {
      uint64_t nextChunk = getNextChunkStart(currentChunk);
      if (nextChunk != UINT64_MAX) {
        std::cout << "Animate: Triggering load of chunk " << nextChunk 
                  << " (progress: " << progressThroughChunk << ")" << std::endl;
        requestLoadIntoInactiveBuffer(nextChunk);
      }
    }
    
    // Also trigger loading for the chunk after next (if we have time)
    if (progressThroughChunk > 0.25f) {
      uint64_t nextNextChunk = getNextChunkStart(getNextChunkStart(currentChunk));
      if (nextNextChunk != UINT64_MAX) {
        requestLoadIntoInactiveBuffer(nextNextChunk);
      }
    }
  }

  void onInit()  {
    std::cout << "\n=== 54-Channel Audio Player ===" << std::endl;
    std::cout << "Current path: " << al::File::currentPath() << std::endl;

    // Enable streaming mode for large files
    streamingMode = true; // should make this dynamically set able 
    std::cout << "Streaming mode: ENABLED (for large file support)" << std::endl;

    // populate audioFiles from folder and pick selectedFileIndex
    scanAudioFiles();
    if (audioFiles.empty()) {
      std::cerr << "✗ ERROR: No audio files found in: " << al::File::currentPath() + audioFolder << std::endl;
      std::cerr << "Please update the audioFolder or add files." << std::endl;
      // quit();
      return;
    }
    if (selectedFileIndex < 0 || selectedFileIndex >= static_cast<int>(audioFiles.size())) selectedFileIndex = 0;

    // Load the selected file (loadAudioFile prints details)
    if (!loadAudioFile(audioFiles[selectedFileIndex])) {
      std::cerr << "✗ ERROR: Could not open selected audio file." << std::endl;
      // quit();
      return;
    }

    // Ensure buffers/meters sized (loadAudioFile already resizes but keep safe)
    int framesPerBuffer = 512;
    buffer.resize(framesPerBuffer * numChannels);
    channelLevels.resize(expectedChannels, 0.0f);
    channelPeaks.resize(expectedChannels, 0.0f);
    peakHoldCounters.resize(expectedChannels, 0);
    frameCounter.store(0);
    
    // Initialize double buffering system
    initializeDoubleBuffering();
  }

  void onCreate() {
    if (displayGUI) {
      imguiInit();
      std::cout << "GUI initialized" << std::endl;
    }
    else {
      std::cout << "GUI disabled" << std::endl;
    }
  }

  void onDraw(Graphics& g) {
    if (displayGUI) {
      imguiBeginFrame();

    ImGui::Begin("54-Channel Audio Player");

    // File selector dropdown
    ImGui::Text("Audio File:");
    if (!audioFiles.empty()) {
      // Create combo box with available files
      const char* preview = audioFiles[selectedFileIndex].c_str();
      if (ImGui::BeginCombo("##fileselect", preview)) {
        for (int i = 0; i < static_cast<int>(audioFiles.size()); i++) {
          bool isSelected = (selectedFileIndex == i);
          if (ImGui::Selectable(audioFiles[i].c_str(), isSelected)) {
            if (i != selectedFileIndex) {
              selectedFileIndex = i;
              loadAudioFile(audioFiles[i]);
            }
          }
          if (isSelected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      ImGui::SameLine();
      if (ImGui::Button("↻ Refresh")) {
        scanAudioFiles();
      }
    } else {
      ImGui::Text("No audio files found in sourceAudio/");
      if (ImGui::Button("Scan for Files")) {
        scanAudioFiles();
      }
    }

    ImGui::Separator();
    ImGui::Text("File Info:");
    ImGui::Text("  File Channels: %d", numChannels);
    ImGui::Text("  Output Channels: %d", expectedChannels);
    ImGui::Text("  Sample Rate: %d Hz", (int)soundFile.frameRate());
    ImGui::Text("  Duration: %.2f seconds", (double)soundFile.frames() / soundFile.frameRate());

    ImGui::Separator();
    ImGui::Text("Playback:");
    ImGui::Text("  Current Frame: %llu / %llu", frameCounter.load(), (uint64_t)soundFile.frames());
    ImGui::Text("  Current Time: %.2f / %.2f seconds",
                (double)frameCounter.load() / soundFile.frameRate(),
                (double)soundFile.frames() / soundFile.frameRate());

    ImGui::Separator();
    ImGui::Text("Controls:");

    if (ImGui::Button(playing ? "⏸ Pause" : "▶ Play")) {
      playing = !playing;
    }

    ImGui::SameLine();
    if (ImGui::Button("⏹ Stop")) {
      playing = false;
      frameCounter.store(0);
    }

    ImGui::SameLine();
    if (ImGui::Button("⏮ Rewind")) {
      frameCounter.store(0);
    }

    if (ImGui::Checkbox("Loop", &loop)) {
      std::cout << "Loop: " << (loop ? "ON" : "OFF") << std::endl;
    }

    if (ImGui::Checkbox("Streaming Mode", &streamingMode)) {
      std::cout << "Streaming Mode: " << (streamingMode ? "ON" : "OFF") << std::endl;
      // Note: Changing streaming mode requires reloading the file
      if (soundFile.opened()) {
        std::cout << "⚠ Note: Restart required for streaming mode change" << std::endl;
      }
    }

    if (ImGui::SliderFloat("Gain", &gain, 0.0f, 1.0f)) {
      std::cout << "Gain: " << gain << std::endl;
    }

    ImGui::Separator();
    ImGui::Checkbox("Show Channel Meters", &showMeters);

    if (showMeters) {
      ImGui::Text("Channel Levels (dB):");

      // Display meters in a scrollable area
      ImGui::BeginChild("Meters", ImVec2(0, 400), true);

      for (int ch = 0; ch < expectedChannels; ch++) {
        // Convert linear amplitude to dB
        float levelDB = -120.0f; // Silence floor
        if (channelLevels[ch] > 0.0f) {
          levelDB = 20.0f * log10f(channelLevels[ch]);
        }

        float peakDB = -120.0f;
        if (channelPeaks[ch] > 0.0f) {
          peakDB = 20.0f * log10f(channelPeaks[ch]);
        }

        // Clamp to reasonable display range
        levelDB = (levelDB < -60.0f) ? -60.0f : levelDB;
        peakDB = (peakDB < -60.0f) ? -60.0f : peakDB;

        // Normalize to 0-1 range for display (-60dB to 0dB)
        float levelNorm = (levelDB + 60.0f) / 60.0f;
        float peakNorm = (peakDB + 60.0f) / 60.0f;

        // Color based on level (green -> yellow -> red)
        ImVec4 color;
        if (levelNorm < 0.5f) {
          color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
        } else if (levelNorm < 0.85f) {
          color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
        } else {
          color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
        }

        // Channel label and dB value
        ImGui::Text("Ch %2d:", ch + 1);
        ImGui::SameLine(60);

        // Progress bar for meter
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        ImGui::ProgressBar(levelNorm, ImVec2(200, 0), "");
        ImGui::PopStyleColor();

        ImGui::SameLine();

        // Peak indicator (small vertical line)
        if (peakNorm > 0.01f) {
          ImGui::Text("|");
        } else {
          ImGui::Text(" ");
        }

        ImGui::SameLine();

        // Show dB value
        if (levelDB > -60.0f) {
          ImGui::Text("%5.1f dB", levelDB);
        } else {
          ImGui::Text("  -inf");
        }
      }

      ImGui::EndChild();
    }

    ImGui::End();

    imguiEndFrame();
    g.clear(0, 0, 0);
    imguiDraw();
  }
  }

  void onSound(AudioIOData& io) {
    // Check if we have a valid file loaded (Gamma SoundFile doesn't have data member)
    if (!soundFile.opened()) {
      // No file loaded, output silence
      while (io()) {
        for (int ch = 0; ch < io.channelsOut(); ch++) {
          io.out(ch) = 0.0f;
        }
      }
      return;
    }

    uint64_t numFrames = io.framesPerBuffer();

    // Resize buffer if needed
    if (buffer.size() < numFrames * numChannels) {
      buffer.resize(numFrames * numChannels);
    }

    // If not playing, output silence
    if (!playing) {
      while (io()) {
        for (int ch = 0; ch < io.channelsOut(); ch++) {
          io.out(ch) = 0.0f;
        }
      }
      return;
    }

    // Check if we're at the end
    if (frameCounter.load() >= soundFile.frames()) {
      if (loop) {
        frameCounter.store(0);
      } else {
        playing = false;
        while (io()) {
          for (int ch = 0; ch < io.channelsOut(); ch++) {
            io.out(ch) = 0.0f;
          }
        }
        return;
      }
    }

    // Adjust numFrames if we're near the end
    if (frameCounter.load() + numFrames > soundFile.frames()) {
      numFrames = soundFile.frames() - frameCounter.load();
    }

    // Double buffering logic
    float* frames = nullptr;
    if (streamingMode) {
      uint64_t currentFrame = frameCounter.load();
      uint64_t requiredChunkStart = (currentFrame / chunkSize) * chunkSize;
      uint64_t activeChunkStart = getActiveBufferChunkStart();
      
      // Check if we need to switch buffers
      if (requiredChunkStart != activeChunkStart) {
        std::cout << "Buffer switch needed: current=" << activeChunkStart 
                  << ", required=" << requiredChunkStart << ", frame=" << currentFrame << std::endl;
        // Try to switch to buffer containing required chunk
        if (trySwitchToBufferWithChunk(requiredChunkStart)) {
          // Successfully switched - use new buffer with correct offset
          uint64_t localFrame = currentFrame - requiredChunkStart;
          auto& activeBuf = getActiveBuffer();
          
          std::cout << "Using switched buffer: localFrame=" << localFrame 
                    << ", bufferSize=" << activeBuf.size() 
                    << ", channels=" << numChannels << std::endl;
          
          // Bounds check
          if (localFrame + numFrames <= activeBuf.size() / numChannels && localFrame >= 0) {
            frames = &activeBuf[localFrame * numChannels];
            std::cout << "Using buffer data at offset " << (localFrame * numChannels) << std::endl;
          } else {
            // Buffer doesn't have enough data - fallback
            std::cout << "Buffer bounds check failed: localFrame=" << localFrame 
                      << ", numFrames=" << numFrames 
                      << ", bufferFrames=" << (activeBuf.size() / numChannels) << std::endl;
            performDirectRead(requiredChunkStart, numFrames);
            frames = buffer.data();
          }
        } else {
          // No buffer ready - fallback to direct read
          std::cout << "No buffer ready, using direct read" << std::endl;
          performDirectRead(requiredChunkStart, numFrames);
          frames = buffer.data();
        }
      } else {
        // Use current active buffer
        uint64_t localFrame = currentFrame - activeChunkStart;
        auto& activeBuf = getActiveBuffer();
        
        // Bounds check
        if (localFrame + numFrames <= activeBuf.size() / numChannels && localFrame >= 0) {
          frames = &activeBuf[localFrame * numChannels];
        } else {
          // Buffer doesn't have enough data - fallback
          std::cout << "Current buffer bounds check failed: localFrame=" << localFrame 
                    << ", numFrames=" << numFrames 
                    << ", bufferFrames=" << (activeBuf.size() / numChannels) << std::endl;
          performDirectRead(requiredChunkStart, numFrames);
          frames = buffer.data();
        }
      }
    } else {
      // For non-streaming, read directly from file
      soundFile.seek(frameCounter.load(), SEEK_SET);
      soundFile.read(buffer.data(), numFrames);
      frames = buffer.data();
    }

    // Copy interleaved data to buffer
    for (uint64_t i = 0; i < numFrames * numChannels; i++) {
      buffer[i] = frames[i];
    }

    // Deinterleave and output to all channels WITH REMAPPING
    int outputChannels = (numChannels < io.channelsOut()) ? numChannels : io.channelsOut();
    
    // Reset channel levels for this buffer (size to output channels)
    std::vector<float> maxLevels(io.channelsOut(), 0.0f);    for (uint64_t frame = 0; frame < numFrames; frame++) {
      // Clear all outputs first
      for (int ch = 0; ch < io.channelsOut(); ch++) {
        io.out(ch, frame) = 0.0f;
      }

      // Apply channel mapping
      for (int i = 0; i < ChannelMapping::NUM_CHANNELS && i < numChannels; i++) {
        int fileChannel = ChannelMapping::channelMap[i].first;
        int outputChannel = ChannelMapping::channelMap[i].second;

        // Bounds check
        if (fileChannel < numChannels && outputChannel < io.channelsOut()) {
          float sample = buffer[frame * numChannels + fileChannel] * gain;
          io.out(outputChannel, frame) = sample;

          // Track max level for metering (use output channel index for display)
          float absSample = fabsf(sample);
          if (absSample > maxLevels[outputChannel]) {
            maxLevels[outputChannel] = absSample;
          }
        }
      }
    }

    // Update meters with max levels from this buffer
    for (int ch = 0; ch < io.channelsOut(); ch++) {
      // Smooth decay for current level
      channelLevels[ch] = channelLevels[ch] * meterDecayRate;

      // Update with new max if higher
      if (maxLevels[ch] > channelLevels[ch]) {
        channelLevels[ch] = maxLevels[ch];
      }

      // Update peak hold
      if (maxLevels[ch] > channelPeaks[ch]) {
        channelPeaks[ch] = maxLevels[ch];
        peakHoldCounters[ch] = peakHoldFrames;
      } else {
        // Decay peak hold
        if (peakHoldCounters[ch] > 0) {
          peakHoldCounters[ch]--;
        } else {
          channelPeaks[ch] = channelPeaks[ch] * meterDecayRate;
        }
      }
    }

    // Fill remaining frames with silence if we read fewer frames
    for (uint64_t frame = numFrames; frame < io.framesPerBuffer(); frame++) {
      for (int ch = 0; ch < io.channelsOut(); ch++) {
        io.out(ch, frame) = 0.0f;
      }
    }

    frameCounter.fetch_add(numFrames);
  }

  bool onKeyDown(const Keyboard& k) {
    // Play/pause

    if (k.key() == ' ') {
      playing = !playing;
      std::cout << (playing ? "▶ Playing audio" : "⏸ Paused audio") << std::endl;
      //return true;
    }
    // Rewind
    if (k.key() == 'r' || k.key() == 'R') {
      frameCounter.store(0);
      std::cout << "⏮ Rewound to beginning" << std::endl;
      //return true;
    }
    // Toggle loop
    if (k.key() == 'l' || k.key() == 'L') {
      loop = !loop;
      std::cout << "Loop: " << (loop ? "ON" : "OFF") << std::endl;
      //return true;
    }

    // Select audio file via keys '1'..'9' (1 selects first file)
    char c = k.key();
    if (c >= '1' && c <= '9') {
      playing = false; // Pause playback when changing files
      int idx = static_cast<int>(c - '1'); // '1'->0, '2'->1, ...
      if (idx < static_cast<int>(audioFiles.size())) {
        if (idx != selectedFileIndex) {
          selectedFileIndex = idx;
          if (loadAudioFile(audioFiles[selectedFileIndex])) {
            std::cout << "Loaded file [" << selectedFileIndex + 1 << "]: " << audioFiles[selectedFileIndex] << std::endl;
          } else {
            std::cerr << "Failed to load file: " << audioFiles[selectedFileIndex] << std::endl;
          }
        } else {
          std::cout << "Already selected file " << selectedFileIndex + 1 << std::endl;
        }
      } else {
        std::cerr << "No audio file for key '" << c << "' (index " << idx << " out of range)" << std::endl;
      }
      //return true;
    }

    return false;
  }

  void onExit() {
    if (displayGUI) imguiShutdown();
    cleanupDoubleBuffering();
  }
};

#endif // MAINPLAYER_HPP