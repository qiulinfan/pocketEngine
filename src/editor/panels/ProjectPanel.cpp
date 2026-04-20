#include "editor/panels/ProjectPanel.h"
#include "editor/core/EditorDragDrop.h"
#include "imgui.h"
#include "shared/config/SpritesheetConfig.h"
#include "shared/resources/ResourcePath.h"
#include "SDL2/SDL.h"
#include "SDL2_image/SDL_image.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// Project panel: a read-only resources browser for the editor.
// It never mutates runtime or scene-document state directly. Instead, it
// surfaces user intent (open scene / open external editor) through
// ProjectPanelResult so the host can decide what to do next.

namespace EditorPanels {
namespace {

struct ProjectEntry {
    std::filesystem::path path;
    bool is_directory = false;
};

// File type classification drives icon choice plus double-click behavior.
enum class EntryKind {
    Directory,
    Audio,
    Lua,
    Config,
    Template,
    Image,
    Font,
    Scene,
    GenericFile
};

struct EntryVisualStyle {
    const char *icon_asset = "";
    const char *icon_code = "";
    const char *type_label = "";
    ImVec4 color = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
};

struct TileRenderResult {
    bool clicked = false;
    bool double_clicked = false;
    bool expand_toggled = false;
};

struct TextureDrawRect {
    ImVec2 min;
    ImVec2 max;
    bool valid = false;
};

struct ImageTextureCache {
    SDL_Renderer *renderer = nullptr;
    std::unordered_map<std::string, SDL_Texture *> textures;
};

struct SpritesheetPopupState {
    bool open_requested = false;
    std::filesystem::path resources_root;
    std::filesystem::path image_path;
    int rows = 1;
    int columns = 1;
};

std::string ToLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return text;
}

/*
Read one directory and return entries in a stable UI order:
directories first, then files, both alphabetically.
*/
std::vector<ProjectEntry> CollectSortedEntries(const std::filesystem::path &directory_path) {
    std::vector<ProjectEntry> entries;
    if (!std::filesystem::exists(directory_path) ||
        !std::filesystem::is_directory(directory_path)) {
        return entries;
    }

    std::error_code iterate_error;
    // list directories first, then files, both sorted alphabetically
    for (std::filesystem::directory_iterator it(directory_path, iterate_error), end;
         !iterate_error && it != end; it.increment(iterate_error)) {
        const std::filesystem::directory_entry &entry = *it;
        ProjectEntry project_entry;
        project_entry.path = entry.path();
        project_entry.is_directory = entry.is_directory();
        entries.emplace_back(std::move(project_entry));
    }

    std::sort(entries.begin(), entries.end(),
              [](const ProjectEntry &a, const ProjectEntry &b) {
                  if (a.is_directory != b.is_directory) {
                      return a.is_directory > b.is_directory;
                  }
                  return a.path.filename().string() < b.path.filename().string();
              });
    return entries;
}

EntryKind ClassifyEntry(const ProjectEntry &entry) {
    if (entry.is_directory) return EntryKind::Directory;

    const std::string extension = ToLower(entry.path.extension().string());
    if (extension == ".scene") return EntryKind::Scene;
    if (extension == ".lua") return EntryKind::Lua;
    if (extension == ".template") return EntryKind::Template;
    if (extension == ".config") return EntryKind::Config;
    if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" ||
        extension == ".flac" || extension == ".m4a" || extension == ".aac") {
        return EntryKind::Audio;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
        extension == ".bmp" || extension == ".tga" || extension == ".gif" ||
        extension == ".webp") {
        return EntryKind::Image;
    }
    if (extension == ".ttf" || extension == ".otf") return EntryKind::Font;
    return EntryKind::GenericFile;
}

// map a logical entry kind to its tile visuals
EntryVisualStyle GetEntryVisualStyle(EntryKind kind) {
    switch (kind) {
    case EntryKind::Directory:
        return {"", "DIR", "Directory", ImVec4(0.95f, 0.81f, 0.37f, 1.0f)};
    case EntryKind::Audio:
        return {"audio", "AUD", "Audio", ImVec4(0.45f, 0.88f, 0.58f, 1.0f)};
    case EntryKind::Lua:
        return {"component", "LUA", "Lua", ImVec4(0.44f, 0.76f, 0.98f, 1.0f)};
    case EntryKind::Config:
        return {"config", "CFG", "Config", ImVec4(0.70f, 0.74f, 0.79f, 1.0f)};
    case EntryKind::Template:
        return {"template", "TPL", "Template", ImVec4(0.87f, 0.69f, 0.98f, 1.0f)};
    case EntryKind::Image:
        return {"image", "IMG", "Image", ImVec4(0.99f, 0.68f, 0.35f, 1.0f)};
    case EntryKind::Font:
        return {"", "TTF", "Font", ImVec4(0.95f, 0.82f, 0.54f, 1.0f)};
    case EntryKind::Scene:
        return {"scene", "SCN", "Scene", ImVec4(0.50f, 0.90f, 0.86f, 1.0f)};
    default:
        return {"", "FILE", "File", ImVec4(0.85f, 0.85f, 0.85f, 1.0f)};
    }
}

// Bridge Project-panel entry kinds into the external-editor configuration enum
EditorExternalFileType ToExternalFileType(EntryKind kind) {
    switch (kind) {
    case EntryKind::Audio:
        return EditorExternalFileType::Audio;
    case EntryKind::Lua:
        return EditorExternalFileType::Lua;
    case EntryKind::Config:
        return EditorExternalFileType::Config;
    case EntryKind::Template:
        return EditorExternalFileType::Template;
    case EntryKind::Image:
        return EditorExternalFileType::Image;
    case EntryKind::Font:
        return EditorExternalFileType::Font;
    case EntryKind::Scene:
        return EditorExternalFileType::Scene;
    case EntryKind::Directory:
    case EntryKind::GenericFile:
    default:
        return EditorExternalFileType::Generic;
    }
}

struct IconTextureCache {
    SDL_Renderer *renderer = nullptr;
    std::unordered_map<std::string, SDL_Texture *> textures;
};

IconTextureCache &GetIconTextureCache() {
    static IconTextureCache cache;
    return cache;
}

ImageTextureCache &GetImageTextureCache() {
    static ImageTextureCache cache;
    return cache;
}

SpritesheetPopupState &GetSpritesheetPopupState() {
    static SpritesheetPopupState state;
    return state;
}

std::unordered_map<std::string, bool> &GetExpandedSpritesheetState() {
    static std::unordered_map<std::string, bool> state;
    return state;
}

void DestroyTextureCache(ImageTextureCache &cache) {
    for (const auto &entry : cache.textures) {
        if (entry.second != nullptr) {
            SDL_DestroyTexture(entry.second);
        }
    }
    cache.textures.clear();
}

void ResetIconTextureCacheIfRendererChanged(SDL_Renderer *renderer) {
    IconTextureCache &cache = GetIconTextureCache();
    if (cache.renderer == renderer) return;

    for (const auto &entry : cache.textures) {
        if (entry.second != nullptr) {
            SDL_DestroyTexture(entry.second);
        }
    }
    cache.textures.clear();
    cache.renderer = renderer;
}

void ResetImageTextureCacheIfRendererChanged(SDL_Renderer *renderer) {
    ImageTextureCache &cache = GetImageTextureCache();
    if (cache.renderer == renderer) return;

    DestroyTextureCache(cache);
    cache.renderer = renderer;
}

std::string ResolveIconPath(const std::string &icon_asset) {
    if (icon_asset.empty()) return "";
    // Editor-only icons live under .engine so player-facing resources stay clean.
    std::string path = ResourcePath::ResolveResourcePath(
        ResourcePath::EngineSystemImagesRoot(), icon_asset, {".png"});
    if (path.empty()) {
        // Backward compatibility while older projects still keep icons in
        // resources/images/system.
        path = ResourcePath::ResolveResourcePath("resources/images/system",
                                                icon_asset, {".png"});
    }
    return path;
}

// Lazily load one icon texture and cache it for future tiles
SDL_Texture *GetIconTexture(SDL_Renderer *renderer, const std::string &icon_asset) {
    if (renderer == nullptr || icon_asset.empty()) return nullptr;

    ResetIconTextureCacheIfRendererChanged(renderer);
    IconTextureCache &cache = GetIconTextureCache();

    const auto found = cache.textures.find(icon_asset);
    if (found != cache.textures.end()) {
        return found->second;
    }

    const std::string icon_path = ResolveIconPath(icon_asset);
    if (icon_path.empty()) {
        cache.textures.emplace(icon_asset, nullptr);
        return nullptr;
    }

    SDL_Texture *texture = IMG_LoadTexture(renderer, icon_path.c_str());
    cache.textures.emplace(icon_asset, texture);
    return texture;
}

SDL_Texture *GetImageTexture(SDL_Renderer *renderer, const std::filesystem::path &image_path) {
    if (renderer == nullptr || image_path.empty()) return nullptr;

    ResetImageTextureCacheIfRendererChanged(renderer);
    ImageTextureCache &cache = GetImageTextureCache();
    const std::string cache_key = image_path.lexically_normal().string();

    const auto found = cache.textures.find(cache_key);
    if (found != cache.textures.end()) {
        return found->second;
    }

    SDL_Texture *texture = IMG_LoadTexture(renderer, cache_key.c_str());
    cache.textures.emplace(cache_key, texture);
    return texture;
}

TextureDrawRect BuildTextureFitRect(SDL_Texture *texture, const ImVec2 &min_point,
                                    const ImVec2 &max_point) {
    TextureDrawRect draw_rect;
    if (texture == nullptr) return draw_rect;

    int texture_width_i = 0;
    int texture_height_i = 0;
    SDL_QueryTexture(texture, nullptr, nullptr, &texture_width_i, &texture_height_i);
    const float texture_width = static_cast<float>(texture_width_i);
    const float texture_height = static_cast<float>(texture_height_i);
    if (texture_width <= 0.0f || texture_height <= 0.0f) return draw_rect;

    const float available_width = max_point.x - min_point.x;
    const float available_height = max_point.y - min_point.y;
    if (available_width <= 0.0f || available_height <= 0.0f) return draw_rect;

    const float scale = std::min(available_width / texture_width, available_height / texture_height);
    const float draw_width = texture_width * scale;
    const float draw_height = texture_height * scale;
    draw_rect.min = ImVec2(min_point.x + (available_width - draw_width) * 0.5f,
                           min_point.y + (available_height - draw_height) * 0.5f);
    draw_rect.max = ImVec2(draw_rect.min.x + draw_width, draw_rect.min.y + draw_height);
    draw_rect.valid = true;
    return draw_rect;
}

bool IsNonTrivialSpritesheet(const SpritesheetGridSpec &spec) {
    return spec.rows > 1 || spec.columns > 1;
}

void DrawTextureRegionFitCentered(ImDrawList *draw_list, SDL_Texture *texture,
                                  const ImVec2 &min_point,
                                  const ImVec2 &max_point,
                                  const ImVec2 &uv_min,
                                  const ImVec2 &uv_max) {
    if (draw_list == nullptr || texture == nullptr) return;

    const TextureDrawRect draw_rect = BuildTextureFitRect(texture, min_point, max_point);
    if (!draw_rect.valid) return;

    draw_list->AddImage(ImTextureRef((ImTextureID)(intptr_t)texture), draw_rect.min,
                        draw_rect.max, uv_min, uv_max);
}

void DrawTextureFitCentered(ImDrawList *draw_list, SDL_Texture *texture,
                            const ImVec2 &min_point,
                            const ImVec2 &max_point) {
    if (draw_list == nullptr || texture == nullptr) return;

    const TextureDrawRect draw_rect = BuildTextureFitRect(texture, min_point, max_point);
    if (!draw_rect.valid) return;

    draw_list->AddImage(ImTextureRef((ImTextureID)(intptr_t)texture), draw_rect.min, draw_rect.max);
}

void DrawSpritesheetGridOverlay(ImDrawList *draw_list,
                                const TextureDrawRect &draw_rect, int rows,
                                int columns) {
    if (draw_list == nullptr || !draw_rect.valid) return;
    rows = std::max(rows, 1);
    columns = std::max(columns, 1);

    const ImU32 line_color = IM_COL32(120, 210, 255, 210);
    const float width = draw_rect.max.x - draw_rect.min.x;
    const float height = draw_rect.max.y - draw_rect.min.y;

    for (int row = 1; row < rows; ++row) {
        const float y = draw_rect.min.y + height * (static_cast<float>(row) / static_cast<float>(rows));
        draw_list->AddLine(ImVec2(draw_rect.min.x, y), ImVec2(draw_rect.max.x, y),
                           line_color, 1.5f);
    }
    for (int column = 1; column < columns; ++column) {
        const float x = draw_rect.min.x + width * (static_cast<float>(column) / static_cast<float>(columns));
        draw_list->AddLine(ImVec2(x, draw_rect.min.y), ImVec2(x, draw_rect.max.y),
                           line_color, 1.5f);
    }
    draw_list->AddRect(draw_rect.min, draw_rect.max, line_color, 0.0f, 0, 1.5f);
}

void DrawDisclosureGlyph(ImDrawList *draw_list, const ImVec2 &min_point,
                         const ImVec2 &max_point, bool expanded,
                         bool hovered) {
    if (draw_list == nullptr) return;

    const ImU32 fill_color =
        hovered ? IM_COL32(80, 94, 112, 240) : IM_COL32(54, 64, 78, 230);
    draw_list->AddRectFilled(min_point, max_point, fill_color, 4.0f);

    const ImVec2 center((min_point.x + max_point.x) * 0.5f,
                        (min_point.y + max_point.y) * 0.5f);
    const ImU32 glyph_color = IM_COL32(220, 228, 238, 255);
    if (expanded) {
        draw_list->AddTriangleFilled(
            ImVec2(center.x - 4.5f, center.y - 2.5f),
            ImVec2(center.x + 4.5f, center.y - 2.5f),
            ImVec2(center.x, center.y + 4.5f), glyph_color);
        return;
    }
    draw_list->AddTriangleFilled(
        ImVec2(center.x - 2.5f, center.y - 4.5f),
        ImVec2(center.x - 2.5f, center.y + 4.5f),
        ImVec2(center.x + 4.5f, center.y), glyph_color);
}

void OpenSpritesheetPopupForImage(const std::filesystem::path &resources_root,
                                  const std::filesystem::path &image_path) {
    SpritesheetPopupState &popup_state = GetSpritesheetPopupState();
    const SpritesheetGridSpec spec =
        SpritesheetConfig::ReadForImagePath(resources_root, image_path);

    popup_state.resources_root = resources_root.lexically_normal();
    popup_state.image_path = image_path.lexically_normal();
    popup_state.rows = spec.rows;
    popup_state.columns = spec.columns;
    popup_state.open_requested = true;
}

void RenderSpritesheetPopup(SDL_Renderer *renderer) {
    SpritesheetPopupState &popup_state = GetSpritesheetPopupState();
    if (popup_state.open_requested) {
        ImGui::OpenPopup("Spritesheet Editor");
        popup_state.open_requested = false;
    }

    ImGui::SetNextWindowSize(ImVec2(860.0f, 760.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Spritesheet Editor", nullptr,
                                ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    SDL_Texture *texture = GetImageTexture(renderer, popup_state.image_path);
    if (texture == nullptr) {
        ImGui::TextUnformatted("Unable to load this image.");
    } else {
        int texture_width = 0;
        int texture_height = 0;
        SDL_QueryTexture(texture, nullptr, nullptr, &texture_width, &texture_height);

        ImGui::Text("Image: %s", popup_state.image_path.filename().string().c_str());
        ImGui::TextDisabled("%d x %d px", texture_width, texture_height);
        ImGui::Separator();

        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Rows", &popup_state.rows, 1, 4)) {
            popup_state.rows = std::max(popup_state.rows, 1);
            SpritesheetConfig::WriteForImagePath(
                popup_state.resources_root, popup_state.image_path,
                {popup_state.rows, popup_state.columns});
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("Columns", &popup_state.columns, 1, 4)) {
            popup_state.columns = std::max(popup_state.columns, 1);
            SpritesheetConfig::WriteForImagePath(
                popup_state.resources_root, popup_state.image_path,
                {popup_state.rows, popup_state.columns});
        }

        const int sprite_width = std::max(1, texture_width / std::max(popup_state.columns, 1));
        const int sprite_height = std::max(1, texture_height / std::max(popup_state.rows, 1));
        ImGui::TextDisabled("Each sprite: %d x %d px | Total sprites: %d",
                            sprite_width, sprite_height,
                            popup_state.rows * popup_state.columns);
        ImGui::Separator();

        const float preview_height = std::max(260.0f, ImGui::GetContentRegionAvail().y - 48.0f);
        ImGui::BeginChild("spritesheet_preview_child",
                          ImVec2(0.0f, preview_height), true,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 preview_min = ImGui::GetCursorScreenPos();
        const ImVec2 preview_size = ImGui::GetContentRegionAvail();
        const ImVec2 preview_max(preview_min.x + preview_size.x,
                                 preview_min.y + preview_size.y);
        ImGui::InvisibleButton("spritesheet_preview_canvas", preview_size);

        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(preview_min, preview_max,
                                 IM_COL32(24, 27, 33, 255), 8.0f);
        const TextureDrawRect draw_rect = BuildTextureFitRect(
            texture, ImVec2(preview_min.x + 12.0f, preview_min.y + 12.0f),
            ImVec2(preview_max.x - 12.0f, preview_max.y - 12.0f));
        if (draw_rect.valid) {
            draw_list->AddImage(ImTextureRef((ImTextureID)(intptr_t)texture),
                                draw_rect.min, draw_rect.max);
            DrawSpritesheetGridOverlay(draw_list, draw_rect, popup_state.rows,
                                       popup_state.columns);
        }
        ImGui::EndChild();
    }

    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Scene files are handled by the editor itself instead of an external app.
bool IsSceneFile(const ProjectEntry &entry) {
    return ClassifyEntry(entry) == EntryKind::Scene;
}

std::string BuildTreeLabel(const ProjectEntry &entry) {
    return "[DIR] " + entry.path.filename().string();
}

std::string BuildDirectoryCaption(const std::filesystem::path &resources_root,
                                  const std::filesystem::path &directory_path) {
    if (directory_path == resources_root) {
        return "resources/";
    }

    std::error_code relative_error;
    const std::filesystem::path relative_path = std::filesystem::relative( directory_path, resources_root, relative_error);
    if (relative_error || relative_path.empty()) {
        return "resources/";
    }
    return "resources/" + relative_path.generic_string();
}

std::string TruncateTextToWidth(const std::string &text, float max_width) {
    if (text.empty()) return text;
    if (ImGui::CalcTextSize(text.c_str()).x <= max_width) {
        return text;
    }

    const std::string ellipsis = "...";
    std::string clipped = text;
    while (!clipped.empty()) {
        clipped.pop_back();
        const std::string candidate = clipped + ellipsis;
        if (ImGui::CalcTextSize(candidate.c_str()).x <= max_width) {
            return candidate;
        }
    }
    return ellipsis;
}

bool BuildDragResourceName(const std::filesystem::path &entry_path,
                           const std::filesystem::path &resource_root,
                           const std::string &expected_extension,
                           std::string &out_resource_name) {
    if (!ResourcePath::IsPathWithinDirectory(entry_path, resource_root)) {
        return false;
    }

    std::error_code relative_error;
    std::filesystem::path relative_path =
        std::filesystem::relative(entry_path, resource_root, relative_error);
    if (relative_error || relative_path.empty()) {
        return false;
    }

    if (!expected_extension.empty() &&
        ToLower(relative_path.extension().string()) != expected_extension) {
        return false;
    }

    relative_path.replace_extension();
    relative_path = ResourcePath::NormalizeRelativePath(relative_path);
    if (relative_path.empty()) {
        return false;
    }

    out_resource_name = relative_path.generic_string();
    return !out_resource_name.empty();
}

bool BuildLuaComponentTypeName(const std::filesystem::path &entry_path,
                               std::string &out_component_type) {
    if (!ResourcePath::IsPathWithinDirectory(entry_path,
                                             "resources/component_types")) {
        return false;
    }

    if (ToLower(entry_path.extension().string()) != ".lua") {
        return false;
    }

    // Runtime currently registers Lua components by filename stem, not by
    // directory-qualified path. Drag-and-drop needs to match that registry.
    out_component_type = entry_path.stem().string();
    return !out_component_type.empty();
}

void RenderEntryDragSourceIfSupported(const ProjectEntry &entry,
                                      EntryKind entry_kind) {
    std::string payload_value;
    const char *payload_type = nullptr;
    const char *payload_label = nullptr;

    // Only built-in project asset kinds that map cleanly into editor actions
    // become drag sources in this first pass.
    if (entry_kind == EntryKind::Lua &&
        BuildLuaComponentTypeName(entry.path, payload_value)) {
        payload_type = EditorDragDrop::kLuaComponentPayload;
        payload_label = "Lua Component";
    } else if (entry_kind == EntryKind::Template &&
               BuildDragResourceName(entry.path, "resources/actor_templates",
                                     ".template", payload_value)) {
        payload_type = EditorDragDrop::kActorTemplatePayload;
        payload_label = "Actor Template";
    }

    if (payload_type == nullptr) {
        return;
    }

    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(payload_type, payload_value.c_str(),
                                  payload_value.size() + 1);
        ImGui::Text("%s", payload_label);
        ImGui::TextDisabled("%s", payload_value.c_str());
        ImGui::EndDragDropSource();
    }
}

// Render the left-side directory navigator recursively
void RenderDirectoryTree(const std::filesystem::path &resources_root,
                         const std::filesystem::path &directory_path,
                         std::filesystem::path &selected_directory) {
    const std::vector<ProjectEntry> entries = CollectSortedEntries(directory_path);

    for (const ProjectEntry &entry : entries) {
        if (!entry.is_directory) {
            continue;
        }

        std::error_code relative_error;
        const std::filesystem::path relative_path = std::filesystem::relative(entry.path, resources_root, relative_error);
        if (relative_error) continue;

        ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (entry.path == selected_directory) {
            node_flags |= ImGuiTreeNodeFlags_Selected;
        }

        const std::string node_id = relative_path.string();
        const std::string label = BuildTreeLabel(entry);
        const bool is_open = ImGui::TreeNodeEx(node_id.c_str(), node_flags, "%s", label.c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            selected_directory = entry.path;
        }

        if (is_open) {
            RenderDirectoryTree(resources_root, entry.path, selected_directory);
            ImGui::TreePop();
        }
    }
}


// Report click state; navigation handled by the parent grid so all entry actions stay centralized
TileRenderResult RenderEntryTile(const ProjectEntry &entry,
                                 EntryKind entry_kind,
                                 const EntryVisualStyle &style,
                                 SDL_Renderer *renderer, bool selected,
                                 const SpritesheetGridSpec &spritesheet_spec,
                                 bool spritesheet_expanded,
                                 float tile_width,
                                 float tile_height) {
    TileRenderResult result;
    const std::string entry_id = entry.path.string();
    ImGui::PushID(entry_id.c_str());

    const ImVec2 tile_size(tile_width, tile_height);
    const ImVec2 tile_min = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("project_entry_tile", tile_size);
    const bool hovered = ImGui::IsItemHovered();
    result.clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    result.double_clicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    RenderEntryDragSourceIfSupported(entry, entry_kind);

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImVec2 tile_max(tile_min.x + tile_size.x, tile_min.y + tile_size.y);

    const ImVec4 tile_bg = selected  ? ImVec4(0.25f, 0.34f, 0.45f, 1.0f)
                                     : hovered ? ImVec4(0.20f, 0.25f, 0.31f, 1.0f)
                                               : ImVec4(0.14f, 0.17f, 0.21f, 1.0f);
    const ImVec4 tile_border = selected ? ImVec4(0.57f, 0.76f, 0.96f, 1.0f)
                                        : hovered ? ImVec4(0.41f, 0.53f, 0.69f, 1.0f)
                                                  : ImVec4(0.23f, 0.28f, 0.34f, 1.0f);
    draw_list->AddRectFilled(tile_min, tile_max, ImGui::GetColorU32(tile_bg), 8.0f);
    draw_list->AddRect(tile_min, tile_max, ImGui::GetColorU32(tile_border), 8.0f);

    // Top icon block: prefer a loaded PNG icon, otherwise fall back to text.
    const ImVec2 icon_min(tile_min.x + 10.0f, tile_min.y + 10.0f);
    const ImVec2 icon_max(tile_max.x - 10.0f, tile_min.y + tile_height * 0.60f);
    draw_list->AddRectFilled(icon_min, icon_max, ImGui::GetColorU32(style.color), 6.0f);
    const bool spritesheet_expandable =
        entry_kind == EntryKind::Image && IsNonTrivialSpritesheet(spritesheet_spec);
    SDL_Texture *image_texture = nullptr;
    if (entry_kind == EntryKind::Image) {
        image_texture = GetImageTexture(renderer, entry.path);
    }
    SDL_Texture *icon_texture = GetIconTexture(renderer, style.icon_asset);

    if (image_texture != nullptr) {
        const ImVec2 image_min(icon_min.x + 6.0f, icon_min.y + 6.0f);
        const ImVec2 image_max(icon_max.x - 6.0f, icon_max.y - 6.0f);
        draw_list->AddRectFilled(image_min, image_max, IM_COL32(20, 23, 29, 230),
                                 4.0f);
        DrawTextureFitCentered(draw_list, image_texture, image_min, image_max);
    } else if (icon_texture != nullptr) {
        const ImVec2 image_min(icon_min.x + 8.0f, icon_min.y + 6.0f);
        const ImVec2 image_max(icon_max.x - 8.0f, icon_max.y - 22.0f);
        DrawTextureFitCentered(draw_list, icon_texture, image_min, image_max);

        const ImVec2 type_size = ImGui::CalcTextSize(style.type_label);
        const ImVec2 type_pos((icon_min.x + icon_max.x - type_size.x) * 0.5f, icon_max.y - type_size.y - 4.0f);
        draw_list->AddText(
            type_pos,
            ImGui::GetColorU32(ImVec4(0.12f, 0.12f, 0.13f, 1.0f)),
            style.type_label);
    } else {
        const ImVec2 icon_size = ImGui::CalcTextSize(style.icon_code);
        const ImVec2 icon_pos((icon_min.x + icon_max.x - icon_size.x) * 0.5f,
                              (icon_min.y + icon_max.y - icon_size.y) * 0.5f);
        draw_list->AddText(
            icon_pos,
            ImGui::GetColorU32(ImVec4(0.12f, 0.12f, 0.13f, 1.0f)),
            style.icon_code);
    }

    const std::string file_name = entry.path.filename().string();
    const std::string visible_name = TruncateTextToWidth(file_name, tile_width - 12.0f);
    const ImVec2 name_size = ImGui::CalcTextSize(visible_name.c_str());
    const ImVec2 name_pos((tile_min.x + tile_max.x - name_size.x) * 0.5f, tile_max.y - 24.0f);
    draw_list->AddText(name_pos, ImGui::GetColorU32(ImVec4(0.92f, 0.94f, 0.97f, 1.0f)), visible_name.c_str());

    if (spritesheet_expandable) {
        const ImVec2 toggle_min(tile_max.x - 28.0f, tile_min.y + 8.0f);
        const ImVec2 toggle_max(tile_max.x - 10.0f, tile_min.y + 26.0f);
        const bool toggle_hovered =
            ImGui::IsMouseHoveringRect(toggle_min, toggle_max);
        if (toggle_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            result.expand_toggled = true;
            result.double_clicked = false;
        }
        DrawDisclosureGlyph(draw_list, toggle_min, toggle_max,
                            spritesheet_expanded, toggle_hovered);

        const std::string badge_text =
            std::to_string(spritesheet_spec.rows) + "x" +
            std::to_string(spritesheet_spec.columns);
        const ImVec2 badge_text_size = ImGui::CalcTextSize(badge_text.c_str());
        const ImVec2 badge_min(icon_min.x + 6.0f,
                               icon_max.y - badge_text_size.y - 10.0f);
        const ImVec2 badge_max(badge_min.x + badge_text_size.x + 10.0f,
                               badge_min.y + badge_text_size.y + 6.0f);
        draw_list->AddRectFilled(badge_min, badge_max,
                                 IM_COL32(22, 26, 31, 220), 4.0f);
        draw_list->AddText(ImVec2(badge_min.x + 5.0f, badge_min.y + 3.0f),
                           IM_COL32(216, 226, 236, 255), badge_text.c_str());
    }

    ImGui::PopID();
    return result;
}

void RenderSpritesheetSpriteTile(SDL_Texture *texture, int rows, int columns,
                                 int row_index, int column_index,
                                 float tile_width, float tile_height) {
    const ImVec2 tile_min = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("spritesheet_sprite_tile",
                           ImVec2(tile_width, tile_height));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 tile_max(tile_min.x + tile_width, tile_min.y + tile_height);
    ImDrawList *draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(
        tile_min, tile_max,
        hovered ? IM_COL32(34, 41, 50, 255) : IM_COL32(26, 31, 38, 255), 7.0f);
    draw_list->AddRect(tile_min, tile_max, IM_COL32(58, 68, 82, 255), 7.0f);

    const ImVec2 preview_min(tile_min.x + 8.0f, tile_min.y + 8.0f);
    const ImVec2 preview_max(tile_max.x - 8.0f, tile_min.y + tile_height - 28.0f);
    draw_list->AddRectFilled(preview_min, preview_max, IM_COL32(19, 23, 29, 255),
                             4.0f);

    const ImVec2 uv_min(
        static_cast<float>(column_index) / static_cast<float>(columns),
        static_cast<float>(row_index) / static_cast<float>(rows));
    const ImVec2 uv_max(
        static_cast<float>(column_index + 1) / static_cast<float>(columns),
        static_cast<float>(row_index + 1) / static_cast<float>(rows));
    DrawTextureRegionFitCentered(draw_list, texture, preview_min, preview_max,
                                 uv_min, uv_max);

    const std::string label = "[" + std::to_string(row_index + 1) + "," +
                              std::to_string(column_index + 1) + "]";
    const ImVec2 label_size = ImGui::CalcTextSize(label.c_str());
    draw_list->AddText(
        ImVec2((tile_min.x + tile_max.x - label_size.x) * 0.5f,
               tile_max.y - label_size.y - 8.0f),
        IM_COL32(210, 220, 230, 255), label.c_str());
}

void RenderExpandedSpritesheetTileGrid(const ProjectEntry &entry,
                                       const SpritesheetGridSpec &spec,
                                       SDL_Renderer *renderer) {
    SDL_Texture *texture = GetImageTexture(renderer, entry.path);
    if (texture == nullptr) return;

    const int rows = std::max(spec.rows, 1);
    const int columns = std::max(spec.columns, 1);
    const int sprite_count = rows * columns;
    if (sprite_count <= 1) return;

    ImGui::Indent(18.0f);
    ImGui::TextDisabled("Sprites");
    constexpr float kSpriteTileWidth = 86.0f;
    constexpr float kSpriteTileHeight = 96.0f;
    constexpr float kSpriteTileSpacing = 8.0f;
    const float available_width = ImGui::GetContentRegionAvail().x;
    const int sprite_columns =
        std::max(1, static_cast<int>((available_width + kSpriteTileSpacing) /
                                     (kSpriteTileWidth + kSpriteTileSpacing)));

    int layout_column_index = 0;
    for (int sprite_index = 0; sprite_index < sprite_count; ++sprite_index) {
        if (layout_column_index > 0) {
            ImGui::SameLine(0.0f, kSpriteTileSpacing);
        }

        const int row_index = sprite_index / columns;
        const int column_index = sprite_index % columns;
        ImGui::PushID(sprite_index);
        RenderSpritesheetSpriteTile(texture, rows, columns, row_index,
                                    column_index, kSpriteTileWidth,
                                    kSpriteTileHeight);
        ImGui::PopID();
        layout_column_index = (layout_column_index + 1) % sprite_columns;
    }
    ImGui::Unindent(18.0f);
}

// Render the right-side tile grid for the selected directory.
/*
Double-click behavior:
directory -> navigate, scene -> request open in editor
other file -> request external editor open.
*/
void RenderDirectoryGrid(const std::filesystem::path &resources_root,
                         const std::filesystem::path &directory_path,
                         std::filesystem::path &selected_directory,
                         std::filesystem::path &selected_entry,
                         SDL_Renderer *renderer,
                         ProjectPanelResult &result) {
    const std::vector<ProjectEntry> entries = CollectSortedEntries(directory_path);
    if (entries.empty()) {
        ImGui::TextDisabled("This folder is empty.");
        return;
    }

    constexpr float kTileWidth = 116.0f;
    constexpr float kTileHeight = 118.0f;
    constexpr float kTileSpacing = 12.0f;

    const float available_width = ImGui::GetContentRegionAvail().x;
    const int columns = std::max( 1, static_cast<int>((available_width + kTileSpacing) / (kTileWidth + kTileSpacing)));

    /*
    Delay directory navigation until after the loop 
    so tile rendering and selection logic use a stable current-directory snapshot this frame
    */
    std::filesystem::path next_directory;
    int column_index = 0;
    for (const ProjectEntry &entry : entries) {
        if (column_index > 0) {
            ImGui::SameLine(0.0f, kTileSpacing);
        }

        const EntryKind entry_kind = ClassifyEntry(entry);
        const EntryVisualStyle style = GetEntryVisualStyle(entry_kind);
        const SpritesheetGridSpec spritesheet_spec =
            (entry_kind == EntryKind::Image)
                ? SpritesheetConfig::ReadForImagePath(resources_root, entry.path)
                : SpritesheetGridSpec{};
        std::unordered_map<std::string, bool> &expanded_state =
            GetExpandedSpritesheetState();
        const std::string expanded_key = entry.path.lexically_normal().string();
        const bool is_spritesheet_expanded =
            expanded_state.find(expanded_key) != expanded_state.end() &&
            expanded_state[expanded_key];
        const bool is_selected = selected_entry == entry.path;
        const TileRenderResult tile_result =
            RenderEntryTile(entry, entry_kind, style, renderer, is_selected,
                            spritesheet_spec, is_spritesheet_expanded,
                            kTileWidth, kTileHeight);

        if (tile_result.clicked) {
            selected_entry = entry.path;
        }
        if (tile_result.expand_toggled && IsNonTrivialSpritesheet(spritesheet_spec)) {
            expanded_state[expanded_key] = !is_spritesheet_expanded;
        }
        if (tile_result.double_clicked) {
            if (entry.is_directory) {
                next_directory = entry.path;
            } else if (IsSceneFile(entry)) {
                result.open_scene_requested = true;
                result.requested_scene_path = entry.path.lexically_normal();
            } else if (entry_kind == EntryKind::Image) {
                OpenSpritesheetPopupForImage(resources_root, entry.path);
            } else {
                result.open_external_editor_requested = true;
                result.requested_external_file_type = ToExternalFileType(entry_kind);
                result.requested_external_file_path = entry.path.lexically_normal();
                result.requested_external_resources_root = resources_root.lexically_normal();
            }
        }
        column_index = (column_index + 1) % columns;

        const bool now_expanded =
            IsNonTrivialSpritesheet(spritesheet_spec) &&
            expanded_state.find(expanded_key) != expanded_state.end() &&
            expanded_state[expanded_key];
        if (!now_expanded) continue;

        if (column_index != 0) {
            ImGui::NewLine();
            column_index = 0;
        }
        RenderExpandedSpritesheetTileGrid(entry, spritesheet_spec, renderer);
        ImGui::Spacing();
    }

    if (!next_directory.empty()) {
        selected_directory = next_directory;
        selected_entry.clear();
    }
}

} // namespace

// Render a read-only project browser tree for the resources directory.
ProjectPanelResult RenderProjectPanel( const std::filesystem::path &resources_root, SDL_Renderer *renderer) {
    // Project panel owns only UI-navigation state. The actual file-system state
    // lives on disk, and open requests are returned to the host as a result.
    static std::filesystem::path selected_directory;
    static std::filesystem::path selected_entry;
    ProjectPanelResult result;
    ResetIconTextureCacheIfRendererChanged(renderer);
    ResetImageTextureCacheIfRendererChanged(renderer);

    ImGui::Begin("Project");

    if (!std::filesystem::exists(resources_root) ||
        !std::filesystem::is_directory(resources_root)) {
        ImGui::TextUnformatted("resources directory not found.");
        ImGui::End();
        return result;
    }

    // If the remembered directory disappeared, fall back to the resources root.
    if (selected_directory.empty() || !std::filesystem::exists(selected_directory) ||
        !std::filesystem::is_directory(selected_directory)) {
        selected_directory = resources_root;
        selected_entry.clear();
    }

    const std::filesystem::path previous_directory = selected_directory;
    const ImGuiTableFlags layout_flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("project_layout", 2, layout_flags)) {
        ImGui::TableSetupColumn("Tree", ImGuiTableColumnFlags_WidthStretch, 0.32f);
        ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch, 0.68f);

        // Left: folder tree navigator.
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Folders");
        ImGui::Separator();
        ImGui::BeginChild("project_tree", ImVec2(0.0f, 0.0f), false);
        ImGuiTreeNodeFlags root_flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selected_directory == resources_root) {
            root_flags |= ImGuiTreeNodeFlags_Selected;
        }
        const bool root_open = ImGui::TreeNodeEx("resources_root", root_flags, "[DIR] resources");
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            selected_directory = resources_root;
        }
        if (root_open) {
            RenderDirectoryTree(resources_root, resources_root, selected_directory);
            ImGui::TreePop();
        }
        ImGui::EndChild();

        // Right: Unity-like tile grid for entries in the current folder.
        ImGui::TableNextColumn();
        ImGui::Text("Current Folder: %s", BuildDirectoryCaption(resources_root, selected_directory).c_str());
        ImGui::TextDisabled("click folder to enter, double-click scene to open.");
        ImGui::Separator();
        ImGui::BeginChild("project_grid", ImVec2(0.0f, 0.0f), false);
        RenderDirectoryGrid(resources_root, selected_directory,
                            selected_directory, selected_entry, renderer, result);
        ImGui::EndChild();

        ImGui::EndTable();
    }

    if (selected_directory != previous_directory) {
        selected_entry.clear();
    }

    RenderSpritesheetPopup(renderer);
    ImGui::End();
    return result;
}

}
