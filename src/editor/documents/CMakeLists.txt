target_sources(
    editor_core
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/SceneDocument.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SceneMutations.cpp"
        "${PROJECT_SOURCE_DIR}/include/editor/documents/SceneDocument.h"
)
