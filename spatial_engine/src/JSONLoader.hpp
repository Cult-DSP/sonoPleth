#pragma once

#include <string>
#include <map>
#include <vector>

struct Keyframe {
    double time;
    float x, y, z;
};

struct SpatialData {
    int sampleRate;
    std::map<std::string, std::vector<Keyframe>> sources;
};

class JSONLoader {
public:
    static SpatialData loadSpatialInstructions(const std::string &path);
};
