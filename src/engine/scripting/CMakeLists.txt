target_sources(
    engine_runtime
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/EventBus.cpp"
        "${PROJECT_SOURCE_DIR}/include/engine/scripting/APIRegistration.h"
        "${PROJECT_SOURCE_DIR}/include/engine/scripting/ComponentManager.h"
        "${PROJECT_SOURCE_DIR}/include/engine/scripting/EventBus.h"
)

add_subdirectory(luaapi)
add_subdirectory(manager)
