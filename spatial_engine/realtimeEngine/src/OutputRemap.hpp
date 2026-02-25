// OutputRemap.hpp — Agent 6: Output Channel Remapping
//
// Maps internal render-buffer channel indices ("layout") to physical
// AudioIO output channel indices ("device") at the very end of the audio
// callback, replacing the current identity-copy loop in Spatializer::renderBlock().
//
// RESPONSIBILITIES:
// - Load a CSV file (once, at startup) specifying layout→device channel pairs.
// - Expose an RT-safe, immutable remap table to the audio thread.
// - Apply the remap in the Spatializer copy step: identity fast-path when no
//   CSV is provided; accumulate loop otherwise.
//
// CSV FORMAT (agent_output_remap.md spec):
//   layout,device
//   0,0
//   1,16
//   ...
//   - Both columns 0-based.
//   - Extra columns ignored.
//   - Lines starting with '#' and empty lines skipped.
//   - Out-of-range entries are dropped (logged once to stderr, never per-frame).
//   - Multiple layout → same device rows are valid (accumulated/summed).
//
// IDENTITY FAST PATH:
//   If no CSV is provided, or the CSV maps exactly layout==device for all
//   active channels with no gaps, `identity=true` is set and the Spatializer
//   falls back to its existing direct-copy loop (zero overhead vs. Phase 6).
//
// REAL-TIME SAFETY:
//   - No allocation, no file I/O, no locks in the audio path.
//   - The entries vector and identity flag are set once during load() and
//     are read-only during playback. The audio thread holds a const pointer.
//
// PROVENANCE:
//   - CSV schema: agent_output_remap.md
//   - Allosphere-specific layout reference: channelMapping.hpp (reference only,
//     not included here — the CSV externalises that knowledge).
//   - Accumulate pattern: adapted from channelMapping.hpp's defaultChannelMap
//     iteration in mainplayer.hpp.

#pragma once

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// RemapEntry — one (layout, device) pair from the CSV
// ─────────────────────────────────────────────────────────────────────────────

struct RemapEntry {
    int layout;   // source channel in the render buffer (0-based)
    int device;   // destination channel in AudioIO output (0-based)
};

// ─────────────────────────────────────────────────────────────────────────────
// OutputRemap — the remap table loaded from a CSV
// ─────────────────────────────────────────────────────────────────────────────

class OutputRemap {
public:

    // Default constructor → identity mapping (no CSV).
    OutputRemap() = default;

    // ── Load from CSV ─────────────────────────────────────────────────────
    // Call once on the main thread before the audio callback starts.
    // Returns true on success (even if some rows were skipped).
    // On failure (file not found / no valid rows) falls back to identity.
    //
    // renderChannels: number of channels in the internal render buffer.
    //   Out-of-range `layout` entries (>= renderChannels) are dropped.
    // deviceChannels: number of channels AudioIO was opened with.
    //   Out-of-range `device` entries (>= deviceChannels) are dropped.
    bool load(const std::string& csvPath,
              int renderChannels,
              int deviceChannels) {
        mEntries.clear();
        mIdentity = true;
        mMaxDeviceIndex = -1;

        std::ifstream file(csvPath);
        if (!file.is_open()) {
            std::cerr << "[OutputRemap] WARNING: Could not open remap CSV: "
                      << csvPath << " — using identity mapping." << std::endl;
            return false;
        }

        // ── Find header line ─────────────────────────────────────────────
        // Accept case-insensitive "layout" and "device" columns anywhere in
        // the first non-comment, non-empty line.
        int layoutCol = -1;
        int deviceCol = -1;
        std::string line;
        while (std::getline(file, line)) {
            trim(line);
            if (line.empty() || line[0] == '#') continue;

            // Parse header
            std::vector<std::string> cols = splitCSV(line);
            for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
                std::string h = cols[i];
                toLower(h);
                if (h == "layout") layoutCol = i;
                else if (h == "device") deviceCol = i;
            }
            break;
        }

        if (layoutCol < 0 || deviceCol < 0) {
            std::cerr << "[OutputRemap] WARNING: CSV missing 'layout' or 'device' header in "
                      << csvPath << " — using identity mapping." << std::endl;
            return false;
        }

        // ── Parse data rows ──────────────────────────────────────────────
        int rowsRead    = 0;
        int rowsDropped = 0;

        while (std::getline(file, line)) {
            trim(line);
            if (line.empty() || line[0] == '#') continue;

            std::vector<std::string> cols = splitCSV(line);
            int maxCol = std::max(layoutCol, deviceCol);
            if (static_cast<int>(cols.size()) <= maxCol) {
                ++rowsDropped;
                continue;
            }

            int lay = -1, dev = -1;
            try {
                lay = std::stoi(cols[layoutCol]);
                dev = std::stoi(cols[deviceCol]);
            } catch (...) {
                ++rowsDropped;
                continue;
            }

            // Range checks
            if (lay < 0 || lay >= renderChannels) {
                ++rowsDropped;
                continue;
            }
            if (dev < 0 || dev >= deviceChannels) {
                ++rowsDropped;
                continue;
            }

            mEntries.push_back({lay, dev});
            if (dev > mMaxDeviceIndex) mMaxDeviceIndex = dev;
            ++rowsRead;
        }

        if (rowsDropped > 0) {
            std::cerr << "[OutputRemap] " << rowsDropped
                      << " row(s) dropped (out-of-range or malformed)." << std::endl;
        }

        if (rowsRead == 0) {
            std::cerr << "[OutputRemap] WARNING: No valid rows in "
                      << csvPath << " — using identity mapping." << std::endl;
            mIdentity = true;
            return false;
        }

        // ── Detect if this is a pure identity map ────────────────────────
        // True iff entries cover 0..N-1 consecutively with layout==device.
        mIdentity = checkIdentity(renderChannels, deviceChannels);

        std::cout << "[OutputRemap] Loaded " << rowsRead << " entries from "
                  << csvPath
                  << (mIdentity ? " (identity map — fast path active)" : " (non-identity remap)")
                  << std::endl;
        return true;
    }

    // ── Accessors (read-only, safe to call from audio thread) ────────────

    bool identity() const { return mIdentity; }
    int  maxDeviceIndex() const { return mMaxDeviceIndex; }
    const std::vector<RemapEntry>& entries() const { return mEntries; }

    // ── Summary for logging ───────────────────────────────────────────────
    void print() const {
        if (mIdentity) {
            std::cout << "[OutputRemap] Identity mapping (no remapping applied)." << std::endl;
            return;
        }
        std::cout << "[OutputRemap] " << mEntries.size()
                  << " active entries, max device ch=" << mMaxDeviceIndex << ":" << std::endl;
        for (const auto& e : mEntries) {
            std::cout << "  layout " << e.layout << " → device " << e.device << std::endl;
        }
    }

private:

    // ── Data ──────────────────────────────────────────────────────────────
    std::vector<RemapEntry> mEntries;
    int  mMaxDeviceIndex = -1;
    bool mIdentity       = true;   // true by default (no CSV)

    // ── Helpers ──────────────────────────────────────────────────────────

    // Check if the current entries constitute a pure identity map
    // (entry[i] = {i, i} for i in 0..N-1, no duplicates, complete coverage).
    bool checkIdentity(int renderChannels, int /*deviceChannels*/) const {
        if (static_cast<int>(mEntries.size()) != renderChannels) return false;
        // Build a coverage bitset
        std::vector<bool> covered(renderChannels, false);
        for (const auto& e : mEntries) {
            if (e.layout != e.device) return false;
            if (covered[e.layout])   return false; // duplicate
            covered[e.layout] = true;
        }
        for (bool c : covered) {
            if (!c) return false;
        }
        return true;
    }

    // Split a CSV line by commas, trimming each field.
    static std::vector<std::string> splitCSV(const std::string& line) {
        std::vector<std::string> result;
        std::istringstream ss(line);
        std::string token;
        while (std::getline(ss, token, ',')) {
            trim(token);
            result.push_back(token);
        }
        return result;
    }

    // In-place trim whitespace from both ends.
    static void trim(std::string& s) {
        const char* ws = " \t\r\n";
        s.erase(0, s.find_first_not_of(ws));
        s.erase(s.find_last_not_of(ws) + 1);
    }

    // In-place ASCII lowercase.
    static void toLower(std::string& s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
};
