/*
  Channel Mapping for 54-Channel Audio Playback System
  
  Maps audio file channels to Allosphere output channels.
  Each pair: { audioFileChannel, allosphereOutputChannel }
  
  Two mapping options available:
  - defaultChannelMap:    0-indexed (for internal use, array indexing)
  - oneIndexedChannelMap: 1-indexed (matches speaker layout JSON, human-readable)
  
  Allosphere Speaker Layout (54 speakers total):
  - Upper Ring (12 speakers):  Allo Ch 1-12   (positive elevation)
  - Middle Ring (30 speakers): Allo Ch 17-46  (ear level, el=0)
  - Lower Ring (12 speakers):  Allo Ch 49-60  (negative elevation)
  
  SKIPPED Allosphere channels: 13-16, 47-48
*/

#ifndef CHANNEL_MAPPING_HPP
#define CHANNEL_MAPPING_HPP

#include <array>
#include <utility>

namespace ChannelMapping {

// Number of channel mappings (54 audio file channels -> 54 speakers)
constexpr int NUM_CHANNELS = 56;

// ============================================================================
// DEFAULT CHANNEL MAP (0-indexed) - Use for array/buffer indexing
// ============================================================================
// Channel mapping pairs: { audioFileChannel, allosphereOutputChannel }
// Both values are 0-indexed (File Ch 0 = first channel, Allo Ch 0 = output 1)
constexpr std::array<std::pair<int, int>, NUM_CHANNELS> defaultChannelMap = {{
    // === UPPER RING (12 speakers) - Allo Ch 0-11 (outputs 1-12) ===
    {0, 0},    // File Ch 0  -> Allo Ch 0  (output 1)
    {1, 1},    // File Ch 1  -> Allo Ch 1  (output 2)
    {2, 2},    // File Ch 2  -> Allo Ch 2  (output 3)
    {3, 3},    // File Ch 3  -> Allo Ch 3  (output 4)
    {4, 4},    // File Ch 4  -> Allo Ch 4  (output 5)
    {5, 5},    // File Ch 5  -> Allo Ch 5  (output 6)
    {6, 6},    // File Ch 6  -> Allo Ch 6  (output 7)
    {7, 7},    // File Ch 7  -> Allo Ch 7  (output 8)
    {8, 8},    // File Ch 8  -> Allo Ch 8  (output 9)
    {9, 9},    // File Ch 9  -> Allo Ch 9  (output 10)
    {10, 10},  // File Ch 10 -> Allo Ch 10 (output 11)
    {11, 11},  // File Ch 11 -> Allo Ch 11 (output 12)
    
    // === MIDDLE RING (30 speakers) - Allo Ch 16-45 (outputs 17-46) ===
    // (Allo outputs 13-16 are SKIPPED)
    {12, 16},  // File Ch 12 -> Allo Ch 16 (output 17)
    {13, 17},  // File Ch 13 -> Allo Ch 17 (output 18)
    {14, 18},  // File Ch 14 -> Allo Ch 18 (output 19)
    {15, 19},  // File Ch 15 -> Allo Ch 19 (output 20)
    {16, 20},  // File Ch 16 -> Allo Ch 20 (output 21)
    {17, 21},  // File Ch 17 -> Allo Ch 21 (output 22)
    {18, 22},  // File Ch 18 -> Allo Ch 22 (output 23)
    {19, 23},  // File Ch 19 -> Allo Ch 23 (output 24)
    {20, 24},  // File Ch 20 -> Allo Ch 24 (output 25)
    {21, 25},  // File Ch 21 -> Allo Ch 25 (output 26)
    {22, 26},  // File Ch 22 -> Allo Ch 26 (output 27)
    {23, 27},  // File Ch 23 -> Allo Ch 27 (output 28)
    {24, 28},  // File Ch 24 -> Allo Ch 28 (output 29)
    {25, 29},  // File Ch 25 -> Allo Ch 29 (output 30)
    {26, 30},  // File Ch 26 -> Allo Ch 30 (output 31)
    {27, 31},  // File Ch 27 -> Allo Ch 31 (output 32)
    {28, 32},  // File Ch 28 -> Allo Ch 32 (output 33)
    {29, 33},  // File Ch 29 -> Allo Ch 33 (output 34)
    {30, 34},  // File Ch 30 -> Allo Ch 34 (output 35)
    {31, 35},  // File Ch 31 -> Allo Ch 35 (output 36)
    {32, 36},  // File Ch 32 -> Allo Ch 36 (output 37)
    {33, 37},  // File Ch 33 -> Allo Ch 37 (output 38)
    {34, 38},  // File Ch 34 -> Allo Ch 38 (output 39)
    {35, 39},  // File Ch 35 -> Allo Ch 39 (output 40)
    {36, 40},  // File Ch 36 -> Allo Ch 40 (output 41)
    {37, 41},  // File Ch 37 -> Allo Ch 41 (output 42)
    {38, 42},  // File Ch 38 -> Allo Ch 42 (output 43)
    {39, 43},  // File Ch 39 -> Allo Ch 43 (output 44)
    {40, 44},  // File Ch 40 -> Allo Ch 44 (output 45)
    {41, 45},  // File Ch 41 -> Allo Ch 45 (output 46)
    
    // === LOWER RING (12 speakers) - Allo Ch 48-59 (outputs 49-60) ===
    // (Allo outputs 47-48 are SKIPPED)
    {42, 48},  // File Ch 42 -> Allo Ch 48 (output 49)
    {43, 49},  // File Ch 43 -> Allo Ch 49 (output 50)
    {44, 50},  // File Ch 44 -> Allo Ch 50 (output 51)
    {45, 51},  // File Ch 45 -> Allo Ch 51 (output 52)
    {46, 52},  // File Ch 46 -> Allo Ch 52 (output 53)
    {47, 53},  // File Ch 47 -> Allo Ch 53 (output 54)
    {48, 54},  // File Ch 48 -> Allo Ch 54 (output 55)
    {49, 55},  // File Ch 49 -> Allo Ch 55 (output 56)
    {50, 56},  // File Ch 50 -> Allo Ch 56 (output 57)
    {51, 57},  // File Ch 51 -> Allo Ch 57 (output 58)
    {52, 58},  // File Ch 52 -> Allo Ch 58 (output 59)
    {53, 59},  // File Ch 53 -> Allo Ch 59 (output 60)
    {55, 47} //Final Channel -> Sub (Channel 48, output 47 when 0 indexed)
}};

// ============================================================================
// 1-INDEXED CHANNEL MAP - Matches speaker layout JSON (human-readable)
// ============================================================================
// Channel mapping pairs: { audioFileChannel, allosphereOutputChannel }
// Both values are 1-indexed (File Ch 1 = first channel, Allo Ch 1 = output 1)
constexpr std::array<std::pair<int, int>, NUM_CHANNELS> oneIndexedChannelMap = {{
    // === UPPER RING (12 speakers) - Allo Ch 1-12 ===
    {1, 1},    // File Ch 1  -> Allo Ch 1
    {2, 2},    // File Ch 2  -> Allo Ch 2
    {3, 3},    // File Ch 3  -> Allo Ch 3
    {4, 4},    // File Ch 4  -> Allo Ch 4
    {5, 5},    // File Ch 5  -> Allo Ch 5
    {6, 6},    // File Ch 6  -> Allo Ch 6
    {7, 7},    // File Ch 7  -> Allo Ch 7
    {8, 8},    // File Ch 8  -> Allo Ch 8
    {9, 9},    // File Ch 9  -> Allo Ch 9
    {10, 10},  // File Ch 10 -> Allo Ch 10
    {11, 11},  // File Ch 11 -> Allo Ch 11
    {12, 12},  // File Ch 12 -> Allo Ch 12
    
    // === MIDDLE RING (30 speakers) - Allo Ch 17-46 ===
    // (Allo Ch 13-16 are SKIPPED)
    {13, 17},  // File Ch 13 -> Allo Ch 17
    {14, 18},  // File Ch 14 -> Allo Ch 18
    {15, 19},  // File Ch 15 -> Allo Ch 19
    {16, 20},  // File Ch 16 -> Allo Ch 20
    {17, 21},  // File Ch 17 -> Allo Ch 21
    {18, 22},  // File Ch 18 -> Allo Ch 22
    {19, 23},  // File Ch 19 -> Allo Ch 23
    {20, 24},  // File Ch 20 -> Allo Ch 24
    {21, 25},  // File Ch 21 -> Allo Ch 25
    {22, 26},  // File Ch 22 -> Allo Ch 26
    {23, 27},  // File Ch 23 -> Allo Ch 27
    {24, 28},  // File Ch 24 -> Allo Ch 28
    {25, 29},  // File Ch 25 -> Allo Ch 29
    {26, 30},  // File Ch 26 -> Allo Ch 30
    {27, 31},  // File Ch 27 -> Allo Ch 31
    {28, 32},  // File Ch 28 -> Allo Ch 32
    {29, 33},  // File Ch 29 -> Allo Ch 33
    {30, 34},  // File Ch 30 -> Allo Ch 34
    {31, 35},  // File Ch 31 -> Allo Ch 35
    {32, 36},  // File Ch 32 -> Allo Ch 36
    {33, 37},  // File Ch 33 -> Allo Ch 37
    {34, 38},  // File Ch 34 -> Allo Ch 38
    {35, 39},  // File Ch 35 -> Allo Ch 39
    {36, 40},  // File Ch 36 -> Allo Ch 40
    {37, 41},  // File Ch 37 -> Allo Ch 41
    {38, 42},  // File Ch 38 -> Allo Ch 42
    {39, 43},  // File Ch 39 -> Allo Ch 43
    {40, 44},  // File Ch 40 -> Allo Ch 44
    {41, 45},  // File Ch 41 -> Allo Ch 45
    {42, 46},  // File Ch 42 -> Allo Ch 46
    
    // === LOWER RING (12 speakers) - Allo Ch 49-60 ===
    // (Allo Ch 47 is skipped and 48 is saved for the sub)
    {43, 49},  // File Ch 43 -> Allo Ch 49
    {44, 50},  // File Ch 44 -> Allo Ch 50
    {45, 51},  // File Ch 45 -> Allo Ch 51
    {46, 52},  // File Ch 46 -> Allo Ch 52
    {47, 53},  // File Ch 47 -> Allo Ch 53
    {48, 54},  // File Ch 48 -> Allo Ch 54
    {49, 55},  // File Ch 49 -> Allo Ch 55
    {50, 56},  // File Ch 50 -> Allo Ch 56
    {51, 57},  // File Ch 51 -> Allo Ch 57
    {52, 58},  // File Ch 52 -> Allo Ch 58
    {53, 59},  // File Ch 53 -> Allo Ch 59
    {54, 60},  // File Ch 54 -> Allo Ch 60
    {56, 48} // Final Channel -> Sub (Channel 48, output 47 when 0 indexed)
}};

// Alias for backward compatibility
constexpr auto& channelMap = defaultChannelMap;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Get Allosphere output channel for a given audio file channel (0-indexed)
inline int getOutputChannel(int audioFileChannel) {
    for (const auto& mapping : defaultChannelMap) {
        if (mapping.first == audioFileChannel) {
            return mapping.second;
        }
    }
    return audioFileChannel; // Default: pass-through if not found
}

// Get audio file channel for a given Allosphere output channel (0-indexed)
inline int getInputChannel(int allosphereChannel) {
    for (const auto& mapping : defaultChannelMap) {
        if (mapping.second == allosphereChannel) {
            return mapping.first;
        }
    }
    return -1; // Not mapped
}

// Get Allosphere output channel for a given audio file channel (1-indexed)
inline int getOutputChannel1Indexed(int audioFileChannel) {
    for (const auto& mapping : oneIndexedChannelMap) {
        if (mapping.first == audioFileChannel) {
            return mapping.second;
        }
    }
    return audioFileChannel; // Default: pass-through if not found
}

// Get audio file channel for a given Allosphere output channel (1-indexed)
inline int getInputChannel1Indexed(int allosphereChannel) {
    for (const auto& mapping : oneIndexedChannelMap) {
        if (mapping.second == allosphereChannel) {
            return mapping.first;
        }
    }
    return -1; // Not mapped
}

// Convert 0-indexed to 1-indexed
inline int toOneIndexed(int zeroIndexed) {
    return zeroIndexed + 1;
}

// Convert 1-indexed to 0-indexed
inline int toZeroIndexed(int oneIndexed) {
    return oneIndexed - 1;
}

} // namespace ChannelMapping

#endif // CHANNEL_MAPPING_HPP