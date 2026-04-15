target_sources(
    engine_runtime
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/AudioManager.cpp"
        "${PROJECT_SOURCE_DIR}/include/engine/audio/AudioHelper.h"
        "${PROJECT_SOURCE_DIR}/include/engine/audio/AudioManager.h"
)
