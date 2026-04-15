add_executable(
    game_editor
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor_bootstrap.cpp"
    "${PROJECT_SOURCE_DIR}/include/app/editor/EditorApp.h"
)
target_link_libraries(game_editor PRIVATE editor_core)
game_engine_apply_cxx_warnings(game_editor)
game_engine_link_sdl(game_editor)
game_engine_stage_resources(game_editor)
add_custom_command(
    TARGET game_editor
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:game_editor>"
        "${PROJECT_SOURCE_DIR}/$<TARGET_FILE_NAME:game_editor>"
    COMMENT "Mirroring game_editor executable to project root"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(
        game_editor
        PROPERTIES
            OUTPUT_NAME "game_editor_linux"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()
