#ifndef SPRITESHEET_CONFIG_H
#define SPRITESHEET_CONFIG_H

#include <filesystem>
#include <string>

struct SpritesheetGridSpec {
    int rows = 1;
    int columns = 1;
};

class SpritesheetConfig {
public:
    // Project-local metadata path used by the editor and runtime alike.
    static std::filesystem::path MetadataPath();

    // Read/write one spritesheet grid spec using a concrete image file path.
    static SpritesheetGridSpec ReadForImagePath(
        const std::filesystem::path &resources_root,
        const std::filesystem::path &image_path);
    static bool WriteForImagePath(const std::filesystem::path &resources_root,
                                  const std::filesystem::path &image_path,
                                  const SpritesheetGridSpec &spec);

    // Resolve metadata for a logical image resource name such as "Run" or
    // "characters/Run" using the same scene-subdirectory preference rules as
    // the renderer texture loader.
    static SpritesheetGridSpec ReadForImageResource(
        const std::string &image_name,
        const std::filesystem::path &preferred_subdirectory = {});
};

#endif
