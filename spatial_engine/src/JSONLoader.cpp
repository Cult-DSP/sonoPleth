#include "JSONLoader.hpp"
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Helper to check if a value is finite
static bool isFiniteValue(double v) {
    return std::isfinite(v);
}

// Helper to check if a keyframe has valid numeric fields
static bool isValidKeyframe(const Keyframe& kf) {
    return isFiniteValue(kf.time) && 
           isFiniteValue(kf.x) && 
           isFiniteValue(kf.y) && 
           isFiniteValue(kf.z);
}

SpatialData JSONLoader::loadSpatialInstructions(const std::string &path) {
    std::ifstream f(path);
    if (!f.good()) throw std::runtime_error("Cannot open spatial JSON");

    json j;
    f >> j;

    SpatialData d;
    d.sampleRate = j["sampleRate"];
    
    // Parse explicit timeUnit from JSON (default: "seconds")
    // This replaces heuristic detection which was error-prone
    std::string timeUnitStr = "seconds";
    if (j.contains("timeUnit") && j["timeUnit"].is_string()) {
        timeUnitStr = j["timeUnit"];
    }
    
    double timeMultiplier = 1.0;  // Conversion factor to seconds
    if (timeUnitStr == "seconds" || timeUnitStr == "s") {
        d.timeUnit = TimeUnit::Seconds;
        timeMultiplier = 1.0;
        std::cout << "Time unit: seconds (no conversion)\n";
    } else if (timeUnitStr == "samples" || timeUnitStr == "samp") {
        d.timeUnit = TimeUnit::Samples;
        timeMultiplier = 1.0 / (double)d.sampleRate;
        std::cout << "Time unit: samples (converting to seconds with sr=" << d.sampleRate << ")\n";
    } else if (timeUnitStr == "milliseconds" || timeUnitStr == "ms") {
        d.timeUnit = TimeUnit::Milliseconds;
        timeMultiplier = 0.001;
        std::cout << "Time unit: milliseconds (converting to seconds)\n";
    } else {
        std::cerr << "Warning: unknown timeUnit '" << timeUnitStr << "', assuming seconds\n";
        d.timeUnit = TimeUnit::Seconds;
        timeMultiplier = 1.0;
    }

    int totalDropped = 0;
    int totalSources = 0;
    
    for (auto &[name, kflist] : j["sources"].items()) {
        std::vector<Keyframe> frames;
        int droppedForSource = 0;
        totalSources++;

        for (auto &k : kflist) {
            Keyframe kf;
            
            // Parse time (required) and convert to seconds
            if (!k.contains("time") || !k["time"].is_number()) {
                droppedForSource++;
                continue;
            }
            kf.time = k["time"].get<double>() * timeMultiplier;
            
            // Parse cart coordinates (required)
            if (!k.contains("cart") || !k["cart"].is_array() || k["cart"].size() < 3) {
                droppedForSource++;
                continue;
            }
            
            kf.x = k["cart"][0];
            kf.y = k["cart"][1];
            kf.z = k["cart"][2];
            
            // Validate: drop keyframes with NaN/Inf values
            if (!isValidKeyframe(kf)) {
                droppedForSource++;
                continue;
            }
            
            // Check for zero-length direction vector
            float mag = std::sqrt(kf.x*kf.x + kf.y*kf.y + kf.z*kf.z);
            if (mag < 1e-8f) {
                // Zero direction - try to fix by using unit Y (front)
                // This happens if distance was incorrectly applied to cart coords
                std::cerr << "Warning: source '" << name << "' keyframe at t=" << kf.time 
                          << " has zero direction, setting to front (0,1,0)\n";
                kf.x = 0.0f;
                kf.y = 1.0f;
                kf.z = 0.0f;
            }
            
            frames.push_back(kf);
        }
        
        if (droppedForSource > 0) {
            std::cerr << "Warning: source '" << name << "' had " << droppedForSource 
                      << " invalid keyframes dropped\n";
            totalDropped += droppedForSource;
        }
        
        // Sort keyframes by time (ascending)
        std::sort(frames.begin(), frames.end(), 
                  [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
        
        // Remove duplicate times (keep last occurrence within epsilon)
        const double timeEpsilon = 1e-6;
        std::vector<Keyframe> deduped;
        for (size_t i = 0; i < frames.size(); i++) {
            // Check if next keyframe has same time (within epsilon)
            if (i + 1 < frames.size() && 
                std::abs(frames[i+1].time - frames[i].time) < timeEpsilon) {
                // Skip this one, keep the later one
                continue;
            }
            deduped.push_back(frames[i]);
        }
        
        if (deduped.size() < frames.size()) {
            std::cerr << "Warning: source '" << name << "' had " 
                      << (frames.size() - deduped.size()) 
                      << " duplicate-time keyframes collapsed\n";
        }
        
        d.sources[name] = deduped;
    }
    
    if (totalDropped > 0) {
        std::cerr << "Total invalid keyframes dropped across all sources: " << totalDropped << "\n";
    }
    
    std::cout << "Loaded " << totalSources << " sources with spatial data\n";

    return d;
}
