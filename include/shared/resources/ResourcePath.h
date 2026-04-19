#ifndef SHARED_RESOURCE_PATH_H
#define SHARED_RESOURCE_PATH_H

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace ResourcePath {

inline std::filesystem::path EngineRootPath() {
    return std::filesystem::path(".engine");
}

inline std::filesystem::path EditorConfigPath() {
    return EngineRootPath() / "editor" / "editor.config";
}

inline std::filesystem::path EngineSystemImagesRoot() {
    return EngineRootPath() / "system" / "images";
}

inline std::filesystem::path EngineSystemIconPath() {
    return EngineRootPath() / "system" / "icon.png";
}

inline std::filesystem::path EngineSystemFontsRoot() {
    return EngineRootPath() / "system" / "fonts";
}

inline bool EnsureDirectoryExists(const std::filesystem::path &directory) {
    if (directory.empty()) return false;
    std::error_code error;
    if (std::filesystem::exists(directory, error)) {
        return !error && std::filesystem::is_directory(directory, error);
    }
    return std::filesystem::create_directories(directory, error);
}

inline std::filesystem::path NormalizeRelativePath( const std::filesystem::path &path) {
    const std::filesystem::path normalized = path.lexically_normal();
    if (normalized == ".") return {};
    return normalized;
}

inline bool IsPathWithinDirectory(const std::filesystem::path &path,
                                  const std::filesystem::path &directory) {
    const std::filesystem::path normalized_path = path.lexically_normal();
    const std::filesystem::path normalized_directory =
        directory.lexically_normal();

    auto path_it = normalized_path.begin();
    auto directory_it = normalized_directory.begin();
    for (; directory_it != normalized_directory.end();
         ++directory_it, ++path_it) {
        if (path_it == normalized_path.end()) return false;
        if (*path_it != *directory_it) return false;
    }
    return true;
}

inline bool IsPathWithinPreferredSubdirectory(
    const std::filesystem::path &relative_parent,
    const std::filesystem::path &preferred_subdirectory) {
    const std::filesystem::path normalized_parent =
        NormalizeRelativePath(relative_parent);
    const std::filesystem::path normalized_preferred =
        NormalizeRelativePath(preferred_subdirectory);
    if (normalized_preferred.empty()) return false;

    auto parent_it = normalized_parent.begin();
    auto preferred_it = normalized_preferred.begin();
    for (; preferred_it != normalized_preferred.end();
         ++preferred_it, ++parent_it) {
        if (parent_it == normalized_parent.end()) return false;
        if (*parent_it != *preferred_it) return false;
    }
    return true;
}

inline int PathPriority(const std::filesystem::path &relative_parent,
                        const std::filesystem::path &preferred_subdirectory) {
    const std::filesystem::path normalized_parent =
        NormalizeRelativePath(relative_parent);
    if (IsPathWithinPreferredSubdirectory(normalized_parent,
                                          preferred_subdirectory)) {
        return 0;
    }
    if (normalized_parent.empty()) return 1;
    return 2;
}

inline bool ComparePathsWithPreference(
    const std::filesystem::path &a, const std::filesystem::path &b,
    const std::filesystem::path &root,
    const std::filesystem::path &preferred_subdirectory) {
    const std::filesystem::path a_relative_parent =
        NormalizeRelativePath(std::filesystem::relative(a.parent_path(), root));
    const std::filesystem::path b_relative_parent =
        NormalizeRelativePath(std::filesystem::relative(b.parent_path(), root));
    const int a_priority =
        PathPriority(a_relative_parent, preferred_subdirectory);
    const int b_priority =
        PathPriority(b_relative_parent, preferred_subdirectory);
    if (a_priority != b_priority) return a_priority < b_priority;
    return a.lexically_normal().string() < b.lexically_normal().string();
}

inline void SortPathsWithPreference(
    std::vector<std::filesystem::path> &paths, const std::filesystem::path &root,
    const std::filesystem::path &preferred_subdirectory) {
    std::sort(paths.begin(), paths.end(),
              [&](const std::filesystem::path &a,
                  const std::filesystem::path &b) {
                  return ComparePathsWithPreference(a, b, root,
                                                    preferred_subdirectory);
              });
}

inline std::vector<std::filesystem::path> CollectFilesRecursively( const std::filesystem::path &root, const std::string &extension = "") {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        return files;
    }

    for (const std::filesystem::directory_entry &entry :
         std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        if (!extension.empty() && entry.path().extension() != extension) continue;
        files.emplace_back(entry.path());
    }
    return files;
}

inline std::string ResolveResourcePath(
    const std::filesystem::path &root, const std::string &resource_name,
    const std::vector<std::string> &default_extensions = {},
    const std::filesystem::path &preferred_subdirectory = {}) {
    if (resource_name.empty()) return "";
    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        return "";
    }

    const std::filesystem::path relative_name =
        NormalizeRelativePath(std::filesystem::path(resource_name));
    if (relative_name.empty()) return "";
    if (relative_name.is_absolute()) return "";

    std::vector<std::filesystem::path> direct_candidates;
    if (default_extensions.empty() || relative_name.has_extension()) {
        direct_candidates.emplace_back((root / relative_name).lexically_normal());
    } else {
        std::filesystem::path base_candidate = root / relative_name;
        for (const std::string &extension : default_extensions) {
            std::filesystem::path candidate = base_candidate;
            candidate += extension;
            direct_candidates.emplace_back(candidate.lexically_normal());
        }
    }

    for (const std::filesystem::path &candidate : direct_candidates) {
        if (!IsPathWithinDirectory(candidate, root)) continue;
        if (std::filesystem::exists(candidate)) return candidate.string();
    }

    std::vector<std::string> target_filenames;
    if (default_extensions.empty() || relative_name.has_extension()) {
        target_filenames.emplace_back(relative_name.filename().string());
    } else {
        for (const std::string &extension : default_extensions) {
            std::filesystem::path filename = relative_name.filename();
            filename += extension;
            target_filenames.emplace_back(filename.string());
        }
    }

    for (const std::string &target_filename : target_filenames) {
        std::vector<std::filesystem::path> matches;
        for (const std::filesystem::path &file : CollectFilesRecursively(root)) {
            if (file.filename() != target_filename) continue;
            matches.emplace_back(file);
        }

        if (matches.empty()) continue;
        SortPathsWithPreference(matches, root, preferred_subdirectory);
        return matches.front().string();
    }

    return "";
}

} // namespace ResourcePath

#endif
