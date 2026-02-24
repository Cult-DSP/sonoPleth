/*
54-Channel Audio Playback System
Plays back a multichannel audio file with all channels mapped to individual outputs.
Includes GUI controls for playback, pause, loop, and rewind.
Includes real-time dB meters for all 54 channels.
*/

#include "mainplayer.hpp"

struct app : App {
  adm_player adm_player_instance;
  app() {
    adm_player_instance.toggleGUI(true); // disable GUI
    adm_player_instance.setSourceAudioFolder("../adm-allo-player/sourceAudio/");
  }
  void onInit() override {
    adm_player_instance.onInit();
    // adm_player_instance.configureAudio(48000,  // Sample rate (adjust to match your file)
    //                  512,    // Buffer size
    //                  60,     // Output channels
    //                  0);     // Input channels

  std::cout << "\n=== Audio Configuration ===" << std::endl;
  std::cout << "Output channels: 60" << std::endl;
  std::cout << "Sample rate: 48000 Hz" << std::endl;
  std::cout << "Buffer size: 512 frames" << std::endl;
  std::cout << "\nKeyboard shortcuts:" << std::endl;
  std::cout << "  SPACE - Play/Pause" << std::endl;
  std::cout << "  R - Rewind" << std::endl;
  std::cout << "  L - Toggle Loop" << std::endl;
  std::cout << "\n" << std::endl;
  }
  void onCreate() override {
    adm_player_instance.onCreate();
  }
  void onDraw(Graphics& g) override {
    adm_player_instance.onDraw(g);
  }
  void onSound(AudioIOData& io) override {
    adm_player_instance.onSound(io);
  }
  bool onKeyDown(const Keyboard& k) override {
    return adm_player_instance.onKeyDown(k);
  }
};

int main() {
  app myApp;
  // adm_player adm_player_instance;

  // adm_player_instance.toggleGUI(false); // disable GUI
  // adm_player_instance.setSourceAudioFolder("../adm-allo-player/sourceAudio/");

  // adm_player_instance.onInit();
  // adm_player_instance.onCreate();


  // Configure audio for 54 output channels, 0 input channels
  // Adjust sample rate and buffer size as needed. (samplerate, buffer size, output channels, input channels)
  myApp.configureAudio(48000, 512, 60, 0); // try 44.1k if there are sampling issuess
  
  myApp.start();
  return 0;
}
