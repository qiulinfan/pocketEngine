#ifndef EDITOR_DRAG_DROP_H
#define EDITOR_DRAG_DROP_H

#include <cstddef>

namespace EditorDragDrop {

// Shared payload IDs keep Project / Hierarchy / Inspector drag-and-drop
// loosely coupled while still speaking the same protocol.
inline constexpr const char *kLuaComponentPayload = "PE_LUA_COMPONENT";
inline constexpr const char *kActorTemplatePayload = "PE_ACTOR_TEMPLATE";
inline constexpr const char *kSceneActorPayload = "PE_SCENE_ACTOR";
inline constexpr const char *kImageAssetPayload = "PE_IMAGE_ASSET";
inline constexpr const char *kAudioAssetPayload = "PE_AUDIO_ASSET";
inline constexpr const char *kSpriteAssetPayload = "PE_SPRITE_ASSET";
inline constexpr std::size_t kMaxResourceNameLength = 256;

struct SpriteAssetPayload {
    char image_resource_name[kMaxResourceNameLength] = {};
    int row = 1;
    int column = 1;
};

} // namespace EditorDragDrop

#endif
