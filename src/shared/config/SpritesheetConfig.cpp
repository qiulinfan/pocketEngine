#include "shared/config/SpritesheetConfig.h"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "shared/resources/ResourcePath.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char *kSpritesheetsKey = "spritesheets";

struct SpritesheetMetadataCache {
    bool loaded = false;
    std::unordered_map<std::string, SpritesheetGridSpec> grid_specs;
};

SpritesheetMetadataCache &GetSpritesheetMetadataCache() {
    static SpritesheetMetadataCache cache;
    return cache;
}

const std::vector<std::string> kSupportedImageExtensions = {
    ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".webp"};

std::string BuildMetadataKey(const std::filesystem::path &resources_root,
                             const std::filesystem::path &image_path) {
    std::error_code relative_error;
    const std::filesystem::path relative_path = std::filesystem::relative(
        image_path.lexically_normal(), resources_root.lexically_normal(),
        relative_error);
    if (relative_error || relative_path.empty()) {
        return image_path.lexically_normal().generic_string();
    }
    return relative_path.generic_string();
}

rapidjson::Document BuildDefaultSpritesheetMetadata() {
    rapidjson::Document document;
    document.SetObject();
    rapidjson::Value spritesheets(rapidjson::kObjectType);
    document.AddMember(kSpritesheetsKey, spritesheets, document.GetAllocator());
    return document;
}

rapidjson::Document ReadSpritesheetMetadataDocument() {
    const std::filesystem::path metadata_path = SpritesheetConfig::MetadataPath();
    if (!std::filesystem::exists(metadata_path)) {
        return BuildDefaultSpritesheetMetadata();
    }

    std::ifstream input_file(metadata_path, std::ios::in);
    if (!input_file.is_open()) {
        return BuildDefaultSpritesheetMetadata();
    }

    std::stringstream content_stream;
    content_stream << input_file.rdbuf();
    rapidjson::Document document;
    document.Parse(content_stream.str().c_str());
    if (!document.IsObject()) {
        return BuildDefaultSpritesheetMetadata();
    }
    if (!document.HasMember(kSpritesheetsKey) ||
        !document[kSpritesheetsKey].IsObject()) {
        return BuildDefaultSpritesheetMetadata();
    }
    return document;
}

bool WriteSpritesheetMetadataDocument(const rapidjson::Document &document) {
    const std::filesystem::path metadata_path = SpritesheetConfig::MetadataPath();
    if (!ResourcePath::EnsureDirectoryExists(metadata_path.parent_path())) {
        return false;
    }

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    std::ofstream output_file(metadata_path, std::ios::out | std::ios::trunc);
    if (!output_file.is_open()) {
        return false;
    }
    output_file << buffer.GetString() << std::endl;
    return true;
}

void EnsureSpritesheetMetadataLoaded() {
    SpritesheetMetadataCache &cache = GetSpritesheetMetadataCache();
    if (cache.loaded) return;

    cache.loaded = true;
    cache.grid_specs.clear();

    const rapidjson::Document document = ReadSpritesheetMetadataDocument();
    const rapidjson::Value &spritesheets = document[kSpritesheetsKey];
    for (auto it = spritesheets.MemberBegin(); it != spritesheets.MemberEnd();
         ++it) {
        if (!it->name.IsString() || !it->value.IsObject()) continue;
        if (!it->value.HasMember("rows") || !it->value["rows"].IsInt()) continue;
        if (!it->value.HasMember("columns") ||
            !it->value["columns"].IsInt()) {
            continue;
        }

        SpritesheetGridSpec spec;
        spec.rows = std::max(1, it->value["rows"].GetInt());
        spec.columns = std::max(1, it->value["columns"].GetInt());
        cache.grid_specs[it->name.GetString()] = spec;
    }
}

bool PersistSpritesheetMetadata() {
    EnsureSpritesheetMetadataLoaded();
    const SpritesheetMetadataCache &cache = GetSpritesheetMetadataCache();

    rapidjson::Document document;
    document.SetObject();
    rapidjson::Value spritesheets(rapidjson::kObjectType);
    for (const auto &entry : cache.grid_specs) {
        rapidjson::Value spec_object(rapidjson::kObjectType);
        spec_object.AddMember("rows", entry.second.rows, document.GetAllocator());
        spec_object.AddMember("columns", entry.second.columns,
                              document.GetAllocator());
        spritesheets.AddMember(
            rapidjson::Value(entry.first.c_str(), document.GetAllocator()).Move(),
            spec_object, document.GetAllocator());
    }
    document.AddMember(kSpritesheetsKey, spritesheets, document.GetAllocator());
    return WriteSpritesheetMetadataDocument(document);
}

} // namespace

std::filesystem::path SpritesheetConfig::MetadataPath() {
    return ResourcePath::EngineRootPath() / "project" / "spritesheets.json";
}

SpritesheetGridSpec SpritesheetConfig::ReadForImagePath(
    const std::filesystem::path &resources_root,
    const std::filesystem::path &image_path) {
    EnsureSpritesheetMetadataLoaded();

    const std::string metadata_key =
        BuildMetadataKey(resources_root, image_path);
    const SpritesheetMetadataCache &cache = GetSpritesheetMetadataCache();
    const auto found = cache.grid_specs.find(metadata_key);
    if (found != cache.grid_specs.end()) {
        return found->second;
    }
    return {};
}

bool SpritesheetConfig::WriteForImagePath(
    const std::filesystem::path &resources_root,
    const std::filesystem::path &image_path,
    const SpritesheetGridSpec &spec) {
    EnsureSpritesheetMetadataLoaded();

    SpritesheetMetadataCache &cache = GetSpritesheetMetadataCache();
    cache.grid_specs[BuildMetadataKey(resources_root, image_path)] = {
        std::max(1, spec.rows), std::max(1, spec.columns)};
    return PersistSpritesheetMetadata();
}

SpritesheetGridSpec SpritesheetConfig::ReadForImageResource(
    const std::string &image_name,
    const std::filesystem::path &preferred_subdirectory) {
    const std::string resolved_image_path = ResourcePath::ResolveResourcePath(
        "resources/images", image_name, kSupportedImageExtensions,
        preferred_subdirectory);
    if (resolved_image_path.empty()) {
        return {};
    }
    return ReadForImagePath("resources", resolved_image_path);
}
