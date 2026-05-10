#include "DefaultLayoutManager.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;
using json = nlohmann::json;

DefaultLayoutManager::DefaultLayoutManager(std::string settingsRootOverride)
    : mSettingsRootOverride(std::move(settingsRootOverride)) {}

fs::path DefaultLayoutManager::layoutPath() const {
    return SpatialRootPaths::defaultLayoutPath(mSettingsRootOverride);
}

fs::path DefaultLayoutManager::metaPath() const {
    return SpatialRootPaths::defaultLayoutMetaPath(mSettingsRootOverride);
}

bool DefaultLayoutManager::hasDefaultLayout() const {
    std::error_code ec;
    return fs::exists(layoutPath(), ec) && fs::is_regular_file(layoutPath(), ec);
}

DefaultLayoutResult DefaultLayoutManager::validateLayoutJson(const std::string& jsonText) {
    DefaultLayoutResult result;
    if (jsonText.empty()) {
        result.message = "Layout JSON is empty.";
        result.status  = DefaultLayoutStatus::Invalid;
        return result;
    }
    try {
        const json j = json::parse(jsonText);
        if (!j.contains("speakers") || !j["speakers"].is_array()) {
            result.message = "Layout JSON is missing the 'speakers' array.";
            result.status  = DefaultLayoutStatus::Invalid;
            return result;
        }
        if (j["speakers"].empty()) {
            result.message = "Layout JSON has an empty 'speakers' array.";
            result.status  = DefaultLayoutStatus::Invalid;
            return result;
        }
        // Check each speaker has required fields.
        for (const auto& spk : j["speakers"]) {
            if (!spk.contains("az") || !spk.contains("el") ||
                !spk.contains("radius") || !spk.contains("channel")) {
                result.message = "One or more speakers are missing required fields (az, el, radius, channel).";
                result.status  = DefaultLayoutStatus::Invalid;
                return result;
            }
        }
        result.success = true;
        result.status  = DefaultLayoutStatus::Loaded;
        result.message = "Valid layout.";
        // Extract optional metadata fields for caller convenience.
        if (j.contains("name") && j["name"].is_string())
            result.layoutName = j["name"].get<std::string>();
    } catch (const json::exception& e) {
        result.message = std::string("JSON parse error: ") + e.what();
        result.status  = DefaultLayoutStatus::Invalid;
    }
    return result;
}

DefaultLayoutResult DefaultLayoutManager::saveDefaultLayout(const std::string& jsonText,
                                                             const std::string& sourcePath) {
    DefaultLayoutResult result;

    // Validate first.
    result = validateLayoutJson(jsonText);
    if (!result.success) return result;

    // Ensure settings directory exists.
    const fs::path settingsDir = SpatialRootPaths::appSettingsRoot(mSettingsRootOverride);
    {
        std::error_code ec;
        fs::create_directories(settingsDir, ec);
        if (ec) {
            result.success = false;
            result.status  = DefaultLayoutStatus::Unavailable;
            result.message = "Cannot create settings directory: " + ec.message();
            return result;
        }
    }

    // Atomic write of layout JSON.
    if (!atomicWrite(layoutPath(), jsonText)) {
        result.success = false;
        result.status  = DefaultLayoutStatus::Unavailable;
        result.message = "Failed to write default_layout.json (check permissions).";
        return result;
    }

    // Write metadata.
    const std::string savedAt = nowUtc();
    std::string layoutName;
    try {
        const json j = json::parse(jsonText);
        if (j.contains("name") && j["name"].is_string())
            layoutName = j["name"].get<std::string>();
    } catch (...) {}

    std::ostringstream meta;
    meta << "{\n"
         << "  \"sourcePath\": \"" << escapeJson(sourcePath) << "\",\n"
         << "  \"savedAt\": \"" << escapeJson(savedAt) << "\",\n"
         << "  \"layoutName\": \"" << escapeJson(layoutName) << "\",\n"
         << "  \"originalFileName\": \"" << escapeJson(fs::path(sourcePath).filename().string()) << "\"\n"
         << "}\n";

    // Metadata write failure is non-fatal — layout file is already committed.
    atomicWrite(metaPath(), meta.str());

    result.success    = true;
    result.status     = DefaultLayoutStatus::Loaded;
    result.message    = "Default layout saved.";
    result.savedAt    = savedAt;
    result.layoutName = layoutName;
    result.sourcePath = sourcePath;
    result.jsonText   = jsonText;
    return result;
}

DefaultLayoutResult DefaultLayoutManager::loadDefaultLayout() const {
    DefaultLayoutResult result;

    const fs::path lp = layoutPath();

    std::error_code ec;
    if (!fs::exists(lp, ec)) {
        result.status  = DefaultLayoutStatus::None;
        result.message = "No default layout saved.";
        return result;
    }
    if (!fs::is_regular_file(lp, ec)) {
        result.status  = DefaultLayoutStatus::Unavailable;
        result.message = "Default layout path exists but is not a regular file.";
        return result;
    }

    std::ifstream f(lp);
    if (!f.is_open()) {
        result.status  = DefaultLayoutStatus::Unavailable;
        result.message = "Cannot open default_layout.json (permission denied?).";
        return result;
    }

    std::string jsonText((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    f.close();

    result = validateLayoutJson(jsonText);
    if (!result.success) return result;

    result.jsonText = jsonText;

    // Load optional metadata.
    const fs::path mp = metaPath();
    if (fs::exists(mp, ec) && fs::is_regular_file(mp, ec)) {
        std::ifstream mf(mp);
        if (mf.is_open()) {
            try {
                std::string metaText((std::istreambuf_iterator<char>(mf)),
                                      std::istreambuf_iterator<char>());
                const json mj = json::parse(metaText);
                if (mj.contains("sourcePath") && mj["sourcePath"].is_string())
                    result.sourcePath = mj["sourcePath"].get<std::string>();
                if (mj.contains("savedAt") && mj["savedAt"].is_string())
                    result.savedAt = mj["savedAt"].get<std::string>();
                if (mj.contains("layoutName") && mj["layoutName"].is_string())
                    result.layoutName = mj["layoutName"].get<std::string>();
            } catch (...) {}
        }
    }

    return result;
}

DefaultLayoutResult DefaultLayoutManager::clearDefaultLayout() {
    DefaultLayoutResult result;
    result.status = DefaultLayoutStatus::None;

    auto removeIfExists = [](const fs::path& p) {
        std::error_code ec;
        if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
            fs::remove(p, ec);
        }
    };

    removeIfExists(layoutPath());
    removeIfExists(metaPath());

    result.success = true;
    result.message = "Default layout cleared.";
    return result;
}

DefaultLayoutStatus DefaultLayoutManager::getDefaultLayoutStatus() const {
    std::error_code ec;
    const fs::path lp = layoutPath();
    if (!fs::exists(lp, ec))           return DefaultLayoutStatus::None;
    if (!fs::is_regular_file(lp, ec))  return DefaultLayoutStatus::Unavailable;
    std::ifstream f(lp);
    if (!f.is_open())                  return DefaultLayoutStatus::Unavailable;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return validateLayoutJson(text).success ? DefaultLayoutStatus::Loaded
                                            : DefaultLayoutStatus::Invalid;
}

bool DefaultLayoutManager::atomicWrite(const fs::path& dest, const std::string& text) {
    const fs::path tmp = fs::path(dest).replace_extension(".json.tmp");
    {
        std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
        if (!out.is_open()) return false;
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out.good()) {
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, dest, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::string DefaultLayoutManager::escapeJson(const std::string& value) {
    std::ostringstream oss;
    for (unsigned char c : value) {
        switch (c) {
            case '\\': oss << "\\\\"; break;
            case '"':  oss << "\\\""; break;
            case '\n': oss << "\\n";  break;
            case '\r': oss << "\\r";  break;
            case '\t': oss << "\\t";  break;
            default:
                if (c < 0x20) {
                    oss << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<int>(c)
                        << std::dec << std::setfill(' ');
                } else {
                    oss << static_cast<char>(c);
                }
        }
    }
    return oss.str();
}

std::string DefaultLayoutManager::nowUtc() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto tt  = clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}
