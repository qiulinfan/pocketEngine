target_sources(
    engine_runtime
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Renderer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/SpriteRenderer.cpp"
        "${PROJECT_SOURCE_DIR}/include/engine/rendering/Renderer.h"
        "${PROJECT_SOURCE_DIR}/include/engine/rendering/SpriteRenderer.h"
)
