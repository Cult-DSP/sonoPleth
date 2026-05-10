#pragma once

#include "SpatialRootPaths.hpp"

#include <filesystem>
#include <string>

// Status of the persisted default speaker layout.
enum class DefaultLayoutStatus {
    None,        // no default has been saved
    Loaded,      // default exists and loaded successfully
    Invalid,     // file exists but failed validation (bad JSON / missing speakers)
    Unavailable, // I/O or permission failure prevented reading
};

struct DefaultLayoutResult {
    bool                success = false;
    DefaultLayoutStatus status  = DefaultLayoutStatus::None;
    std::string         message;     // human-readable detail for GUI display
    std::string         jsonText;    // populated on successful load
    std::string         sourcePath;  // original user path (from metadata, display only)
    std::string         savedAt;
    std::string         layoutName;
};

// Manages the persistent default speaker layout stored in the app settings directory.
//
// The authoritative copy lives at appSettingsRoot/default_layout.json.
// The original user-selected file is never modified or deleted.
// All writes use a temp-file-then-rename strategy to avoid partial writes.
//
// All methods are safe to call from the GUI (main) thread only.
class DefaultLayoutManager {
public:
    // settingsRootOverride: empty = use SpatialRootPaths::appSettingsRoot().
    // Accepts an override so tests can inject a temp directory.
    explicit DefaultLayoutManager(std::string settingsRootOverride = "");

    // Path helpers (derived from the settings root).
    std::filesystem::path layoutPath() const;
    std::filesystem::path metaPath() const;

    // Returns true if default_layout.json exists (does not validate content).
    bool hasDefaultLayout() const;

    // Copy jsonText into the settings dir as default_layout.json.
    // Writes metadata alongside it. Uses atomic rename.
    // sourcePath is stored for display only — not required for future loads.
    DefaultLayoutResult saveDefaultLayout(const std::string& jsonText,
                                          const std::string& sourcePath = "");

    // Load and validate default_layout.json. Populates jsonText on success.
    DefaultLayoutResult loadDefaultLayout() const;

    // Validate a JSON string as a speaker layout (must have non-empty "speakers").
    // Does not touch any files.
    static DefaultLayoutResult validateLayoutJson(const std::string& jsonText);

    // Remove default_layout.json and default_layout.meta.json if present.
    // Never touches the original user file. Best-effort; logs reason on failure.
    DefaultLayoutResult clearDefaultLayout();

    // Quick summary for GUI status display (does not read JSON content).
    DefaultLayoutStatus getDefaultLayoutStatus() const;

private:
    std::string mSettingsRootOverride;

    // Write text to a tmp file in the same dir, then rename to dest.
    // Returns true on success.
    static bool atomicWrite(const std::filesystem::path& dest, const std::string& text);

    static std::string escapeJson(const std::string& value);
    static std::string nowUtc();
};
