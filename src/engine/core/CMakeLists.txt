target_sources(
    engine_runtime
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Engine.cpp"
        "${PROJECT_SOURCE_DIR}/include/engine/core/Helper.h"
        "${PROJECT_SOURCE_DIR}/include/engine/core/Engine.h"
)
