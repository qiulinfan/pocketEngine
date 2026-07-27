add_executable(
    game_editor
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor_bootstrap.cpp"
    "${PROJECT_SOURCE_DIR}/include/app/editor/EditorApp.h"
)
target_link_libraries(game_editor PRIVATE editor_core)
if(TARGET pocket_agent_host)
    add_dependencies(game_editor pocket_agent_host)
endif()
if(WIN32)
    target_link_libraries(game_editor PRIVATE ole32 shell32)
endif()
game_engine_apply_cxx_warnings(game_editor)
game_engine_link_sdl(game_editor)
game_engine_stage_resources(game_editor)
game_engine_mirror_windows_runtime_to_root(game_editor)
set_target_properties(
    game_editor
    PROPERTIES
        OUTPUT_NAME "pocket"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32)
    add_custom_command(
        TARGET game_editor
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:game_editor>"
            "${PROJECT_SOURCE_DIR}/$<TARGET_FILE_NAME:game_editor>"
        COMMENT "Mirroring pocket executable to project root"
    )
    add_custom_target(
        game_editor_root_mirror ALL
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:game_editor>"
            "${PROJECT_SOURCE_DIR}/$<TARGET_FILE_NAME:game_editor>"
        DEPENDS game_editor
        COMMENT "Refreshing root pocket executable"
        VERBATIM
    )
endif()
