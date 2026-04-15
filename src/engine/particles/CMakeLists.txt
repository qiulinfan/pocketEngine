target_sources(
    engine_runtime
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/ParticleManager.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/ParticleSystem.cpp"
        "${PROJECT_SOURCE_DIR}/include/engine/particles/ParticleManager.h"
        "${PROJECT_SOURCE_DIR}/include/engine/particles/ParticleSystem.h"
)
