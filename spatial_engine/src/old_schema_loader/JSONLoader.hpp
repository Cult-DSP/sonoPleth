#pragma once

#include <string>
#include <map>
#include <vector>

struct Keyframe {
    double time;
    float x, y, z;
};

// Time unit for keyframe timestamps
// Used to convert all times to seconds during loading
enum class TimeUnit {
    Seconds,      // Default: times are already in seconds
    Samples,      // Times are sample indices (divide by sampleRate)
    Milliseconds  // Times are in milliseconds (divide by 1000)
};

struct SpatialData {
    int sampleRate;
    TimeUnit timeUnit = TimeUnit::Seconds;  // Explicit time unit from JSON
    std::map<std::string, std::vector<Keyframe>> sources;
};

class JSONLoader {
public:
    static SpatialData loadSpatialInstructions(const std::string &path);
};
