target_sources(
    editor_core
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/EditorBridgeClient.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/EditorBridgeProtocol.cpp"
        "${PROJECT_SOURCE_DIR}/include/editor/bridge/EditorBridgeClient.h"
        "${PROJECT_SOURCE_DIR}/include/editor/bridge/EditorBridgeProtocol.h"
)
