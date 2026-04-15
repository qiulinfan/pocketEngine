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

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(
        game_engine
        PROPERTIES
            OUTPUT_NAME "game_engine_linux"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()
