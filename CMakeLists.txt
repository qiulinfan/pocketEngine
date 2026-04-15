cmake_minimum_required(VERSION 3.21)

project(game_engine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(
    GAME_ENGINE_STAGE_RESOURCES
    "Copy resources/ into the executable output directory after each build."
    ON
)
option(
    GAME_ENGINE_STAGE_SDL_RUNTIME
    "Stage vendored SDL runtime files next to the executable when needed."
    ON
)

if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(CMAKE_XCODE_GENERATE_SCHEME ON)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GameEngineSDL)
game_engine_ensure_sdl()

add_library(game_engine_vendor_headers INTERFACE)
target_include_directories(
    game_engine_vendor_headers
    SYSTEM
    INTERFACE
        "${PROJECT_SOURCE_DIR}/thirdparty"
        "${PROJECT_SOURCE_DIR}/thirdparty/Lua"
)

function(game_engine_stage_resources target)
    if(GAME_ENGINE_STAGE_RESOURCES)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND
                ${CMAKE_COMMAND} -E copy_directory
                "${PROJECT_SOURCE_DIR}/resources"
                "$<TARGET_FILE_DIR:${target}>/resources"
            COMMENT "Staging resources for ${target}"
        )
    endif()
endfunction()

function(game_engine_apply_cxx_warnings target)
    target_compile_definitions(
        ${target}
        PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING>
            $<$<CXX_COMPILER_ID:MSVC>:_SILENCE_CXX17_ITERATOR_DEPRECATION_WARNING>
    )
    target_compile_options(
        ${target}
        PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
            $<$<CXX_COMPILER_ID:MSVC>:/permissive->
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-deprecated-declarations>
    )
endfunction()

add_subdirectory(thirdparty)

add_library(engine_runtime STATIC)
target_include_directories(
    engine_runtime
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/include/engine"
)
target_link_libraries(
    engine_runtime
    PUBLIC
        box2d_runtime
        game_engine_sdl_headers
        game_engine_vendor_headers
    PRIVATE
        lua_runtime
)
game_engine_apply_cxx_warnings(engine_runtime)
set_target_properties(engine_runtime PROPERTIES FOLDER "engine")
add_library(GameEngine::Runtime ALIAS engine_runtime)

add_library(editor_core STATIC)
target_include_directories(
    editor_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(
    editor_core
    PUBLIC
        engine_runtime
        imgui_runtime
        imguizmo_runtime
)
game_engine_apply_cxx_warnings(editor_core)
set_target_properties(editor_core PROPERTIES FOLDER "editor")
add_library(GameEngine::Editor ALIAS editor_core)

add_subdirectory(src/shared)
add_subdirectory(src/engine)
add_subdirectory(src/editor)
add_subdirectory(src/app/runtime)
add_subdirectory(src/app/editor)

if(MSVC)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT game_engine)
    set_target_properties(
        game_engine
        PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:game_engine>"
    )
endif()
