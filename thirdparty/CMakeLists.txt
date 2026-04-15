file(GLOB GAME_ENGINE_LUA_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/thirdparty/Lua/*.h"
    "${PROJECT_SOURCE_DIR}/thirdparty/Lua/*.hpp"
)
file(GLOB GAME_ENGINE_LUA_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/thirdparty/Lua/*.c"
)
list(FILTER GAME_ENGINE_LUA_SOURCES EXCLUDE REGEX "/lua\\.c$")
list(FILTER GAME_ENGINE_LUA_SOURCES EXCLUDE REGEX "/luac\\.c$")
list(SORT GAME_ENGINE_LUA_HEADERS)
list(SORT GAME_ENGINE_LUA_SOURCES)

file(GLOB GAME_ENGINE_BOX2D_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/thirdparty/box2d/*.h"
)
file(GLOB_RECURSE GAME_ENGINE_BOX2D_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/thirdparty/box2d/src/*.cpp"
)
list(SORT GAME_ENGINE_BOX2D_HEADERS)
list(SORT GAME_ENGINE_BOX2D_SOURCES)

add_library(box2d_runtime STATIC)
target_sources(
    box2d_runtime
    PRIVATE
        ${GAME_ENGINE_BOX2D_SOURCES}
        ${GAME_ENGINE_BOX2D_HEADERS}
)
target_include_directories(
    box2d_runtime
    PUBLIC
        "${PROJECT_SOURCE_DIR}/thirdparty"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/thirdparty/box2d/src"
)
target_compile_features(box2d_runtime PUBLIC cxx_std_11)
set_target_properties(box2d_runtime PROPERTIES FOLDER "thirdparty")

add_library(lua_runtime STATIC)
target_sources(
    lua_runtime
    PRIVATE
        ${GAME_ENGINE_LUA_SOURCES}
        ${GAME_ENGINE_LUA_HEADERS}
)
target_include_directories(
    lua_runtime
    PUBLIC
        "${PROJECT_SOURCE_DIR}/thirdparty/Lua"
)
target_compile_definitions(
    lua_runtime
    PRIVATE
        $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)
target_compile_options(
    lua_runtime
    PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/W3>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wextra>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wno-deprecated-declarations>
)
set_target_properties(lua_runtime PROPERTIES FOLDER "thirdparty")

add_library(imgui_runtime STATIC)
target_sources(
    imgui_runtime
    PRIVATE
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui/imgui.cpp"
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui/imgui_draw.cpp"
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui/imgui_tables.cpp"
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui/imgui_widgets.cpp"
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_sdl2.cpp"
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_sdlrenderer2.cpp"
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui/imgui.h"
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui/imgui_internal.h"
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui/imconfig.h"
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_sdl2.h"
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_sdlrenderer2.h"
)
target_include_directories(
    imgui_runtime
    PUBLIC
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui"
        "${PROJECT_SOURCE_DIR}/thirdparty/imgui/backends"
)
target_link_libraries(imgui_runtime PUBLIC game_engine_sdl)
set_target_properties(imgui_runtime PROPERTIES FOLDER "thirdparty")

add_library(imguizmo_runtime STATIC)
target_sources(
    imguizmo_runtime
    PRIVATE
        "${PROJECT_SOURCE_DIR}/thirdparty/imguizmo/ImGuizmo.cpp"
        "${PROJECT_SOURCE_DIR}/thirdparty/imguizmo/ImGuizmo.h"
)
target_include_directories(
    imguizmo_runtime
    PUBLIC
        "${PROJECT_SOURCE_DIR}/thirdparty/imguizmo"
)
target_link_libraries(imguizmo_runtime PUBLIC imgui_runtime)
set_target_properties(imguizmo_runtime PROPERTIES FOLDER "thirdparty")
