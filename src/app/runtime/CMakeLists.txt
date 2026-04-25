add_executable(
    game_engine
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime_bootstrap.cpp"
    "${PROJECT_SOURCE_DIR}/include/app/runtime/RuntimeApp.h"
)
target_link_libraries(game_engine PRIVATE engine_runtime)
game_engine_apply_cxx_warnings(game_engine)
game_engine_link_sdl(game_engine)
game_engine_stage_resources(game_engine)
game_engine_mirror_windows_runtime_to_root(game_engine)
set_target_properties(
    game_engine
    PROPERTIES
        OUTPUT_NAME "game"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32)
    add_custom_command(
        TARGET game_engine
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:game_engine>"
            "${PROJECT_SOURCE_DIR}/$<TARGET_FILE_NAME:game_engine>"
        COMMENT "Mirroring game executable to project root"
    )
endif()
