include_guard(GLOBAL)

add_library(game_engine_sdl_headers INTERFACE)
target_include_directories(
    game_engine_sdl_headers
    SYSTEM
    INTERFACE
        "${PROJECT_SOURCE_DIR}/thirdparty"
)

function(_game_engine_collect_windows_runtime_dlls out_var)
    if(NOT WIN32 OR NOT GAME_ENGINE_STAGE_SDL_RUNTIME)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    file(GLOB _game_engine_runtime_dlls CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/dlls/*.dll"
        "${PROJECT_SOURCE_DIR}/thirdparty/SDL2/lib/*.dll"
        "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_image/lib/*.dll"
        "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_image/lib/optional/*.dll"
        "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_mixer/lib/*.dll"
        "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_mixer/lib/optional/*.dll"
        "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_ttf/lib/*.dll"
    )
    list(REMOVE_DUPLICATES _game_engine_runtime_dlls)
    set(${out_var} "${_game_engine_runtime_dlls}" PARENT_SCOPE)
endfunction()

function(_game_engine_ensure_windows_root_runtime_target)
    if(NOT WIN32 OR NOT GAME_ENGINE_STAGE_SDL_RUNTIME)
        return()
    endif()

    if(TARGET game_engine_windows_root_runtime_dlls)
        return()
    endif()

    _game_engine_collect_windows_runtime_dlls(_game_engine_runtime_dlls)

    set(_game_engine_root_runtime_commands)
    foreach(_game_engine_runtime_dll IN LISTS _game_engine_runtime_dlls)
        list(APPEND _game_engine_root_runtime_commands
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_game_engine_runtime_dll}"
                    "${PROJECT_SOURCE_DIR}"
        )
    endforeach()

    add_custom_target(
        game_engine_windows_root_runtime_dlls
        ${_game_engine_root_runtime_commands}
        VERBATIM
    )
    set_target_properties(game_engine_windows_root_runtime_dlls PROPERTIES
        FOLDER "runtime"
    )
endfunction()

function(game_engine_ensure_sdl)
    if(NOT TARGET game_engine_sdl)
        add_library(game_engine_sdl INTERFACE)
        target_link_libraries(game_engine_sdl INTERFACE game_engine_sdl_headers)

        if(WIN32)
            add_library(game_engine_sdl2 SHARED IMPORTED GLOBAL)
            set_target_properties(
                game_engine_sdl2
                PROPERTIES
                    IMPORTED_IMPLIB "${PROJECT_SOURCE_DIR}/thirdparty/SDL2/lib/SDL2.lib"
                    IMPORTED_LOCATION "${PROJECT_SOURCE_DIR}/thirdparty/SDL2/lib/SDL2.dll"
            )

            add_library(game_engine_sdl2main STATIC IMPORTED GLOBAL)
            set_target_properties(
                game_engine_sdl2main
                PROPERTIES
                    IMPORTED_LOCATION "${PROJECT_SOURCE_DIR}/thirdparty/SDL2/lib/SDL2main.lib"
            )

            add_library(game_engine_sdl2_image SHARED IMPORTED GLOBAL)
            set_target_properties(
                game_engine_sdl2_image
                PROPERTIES
                    IMPORTED_IMPLIB "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_image/lib/SDL2_image.lib"
                    IMPORTED_LOCATION "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_image/lib/SDL2_image.dll"
            )

            add_library(game_engine_sdl2_mixer SHARED IMPORTED GLOBAL)
            set_target_properties(
                game_engine_sdl2_mixer
                PROPERTIES
                    IMPORTED_IMPLIB "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_mixer/lib/SDL2_mixer.lib"
                    IMPORTED_LOCATION "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_mixer/lib/SDL2_mixer.dll"
            )

            add_library(game_engine_sdl2_ttf SHARED IMPORTED GLOBAL)
            set_target_properties(
                game_engine_sdl2_ttf
                PROPERTIES
                    IMPORTED_IMPLIB "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_ttf/lib/SDL2_ttf.lib"
                    IMPORTED_LOCATION "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_ttf/lib/SDL2_ttf.dll"
            )

            target_link_libraries(
                game_engine_sdl
                INTERFACE
                    game_engine_sdl2
                    game_engine_sdl2main
                    game_engine_sdl2_image
                    game_engine_sdl2_mixer
                    game_engine_sdl2_ttf
            )
        elseif(APPLE)
            set(_game_engine_frameworks
                "${PROJECT_SOURCE_DIR}/thirdparty/SDL2/lib/SDL2.framework"
                "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_image/lib/SDL2_image.framework"
                "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_mixer/lib/SDL2_mixer.framework"
                "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_ttf/lib/SDL2_ttf.framework"
            )

            target_include_directories(
                game_engine_sdl
                SYSTEM
                INTERFACE
                    "${PROJECT_SOURCE_DIR}/thirdparty/SDL2/lib/SDL2.framework/Headers"
                    "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_image/lib/SDL2_image.framework/Headers"
                    "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_mixer/lib/SDL2_mixer.framework/Headers"
                    "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_ttf/lib/SDL2_ttf.framework/Headers"
            )
            target_link_libraries(game_engine_sdl INTERFACE ${_game_engine_frameworks})
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
            pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
            pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)
            pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)

            target_link_libraries(
                game_engine_sdl
                INTERFACE
                    PkgConfig::SDL2
                    PkgConfig::SDL2_IMAGE
                    PkgConfig::SDL2_MIXER
                    PkgConfig::SDL2_TTF
            )
        endif()
    endif()
endfunction()

function(game_engine_link_sdl target)
    game_engine_ensure_sdl()
    target_link_libraries(${target} PRIVATE game_engine_sdl)

    if(WIN32 AND GAME_ENGINE_STAGE_SDL_RUNTIME)
        _game_engine_collect_windows_runtime_dlls(_game_engine_runtime_dlls)

        foreach(_game_engine_runtime_dll IN LISTS _game_engine_runtime_dlls)
            add_custom_command(
                TARGET ${target}
                POST_BUILD
                COMMAND
                    ${CMAKE_COMMAND} -E copy_if_different
                    "${_game_engine_runtime_dll}"
                    "$<TARGET_FILE_DIR:${target}>"
                COMMENT "Staging runtime DLLs for ${target}"
            )
        endforeach()
    endif()

    if(APPLE AND GAME_ENGINE_STAGE_SDL_RUNTIME)
        set(_game_engine_frameworks
            "${PROJECT_SOURCE_DIR}/thirdparty/SDL2/lib/SDL2.framework"
            "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_image/lib/SDL2_image.framework"
            "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_mixer/lib/SDL2_mixer.framework"
            "${PROJECT_SOURCE_DIR}/thirdparty/SDL2_ttf/lib/SDL2_ttf.framework"
        )

        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND
                ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${target}>/Frameworks"
            COMMENT "Creating Frameworks directory for ${target}"
        )

        foreach(_game_engine_framework IN LISTS _game_engine_frameworks)
            get_filename_component(_game_engine_framework_name "${_game_engine_framework}" NAME)
            add_custom_command(
                TARGET ${target}
                POST_BUILD
                COMMAND
                    ${CMAKE_COMMAND} -E copy_directory
                    "${_game_engine_framework}"
                    "$<TARGET_FILE_DIR:${target}>/Frameworks/${_game_engine_framework_name}"
                COMMENT "Embedding ${_game_engine_framework_name} for ${target}"
            )
        endforeach()

        set_target_properties(
            ${target}
            PROPERTIES
                BUILD_RPATH "@executable_path/Frameworks"
                INSTALL_RPATH "@executable_path/Frameworks"
        )
    endif()
endfunction()

function(game_engine_mirror_windows_runtime_to_root target)
    if(NOT WIN32 OR NOT GAME_ENGINE_STAGE_SDL_RUNTIME)
        return()
    endif()

    _game_engine_ensure_windows_root_runtime_target()
    add_dependencies(${target} game_engine_windows_root_runtime_dlls)
endfunction()
