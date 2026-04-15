#ifndef EDITOR_DRAG_DROP_H
#define EDITOR_DRAG_DROP_H

namespace EditorDragDrop {

// Shared payload IDs keep Project / Hierarchy / Inspector drag-and-drop
// loosely coupled while still speaking the same protocol.
inline constexpr const char *kLuaComponentPayload = "PE_LUA_COMPONENT";
inline constexpr const char *kActorTemplatePayload = "PE_ACTOR_TEMPLATE";
inline constexpr const char *kSceneActorPayload = "PE_SCENE_ACTOR";

} // namespace EditorDragDrop

#endif
