option(
    POCKET_ENGINE_BUILD_AGENT_HOST
    "Build the TypeScript pocket-agent-host when local npm dependencies exist."
    ON
)

if(POCKET_ENGINE_BUILD_AGENT_HOST)
    find_program(POCKET_ENGINE_NPM_EXECUTABLE npm)
    file(
        GLOB_RECURSE POCKET_AGENT_HOST_TYPESCRIPT_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.ts"
    )
    if(POCKET_ENGINE_NPM_EXECUTABLE AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/node_modules/typescript/bin/tsc")
        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/dist/index.js"
            COMMAND "${POCKET_ENGINE_NPM_EXECUTABLE}" run build
            DEPENDS
                ${POCKET_AGENT_HOST_TYPESCRIPT_SOURCES}
                "${CMAKE_CURRENT_SOURCE_DIR}/package.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/package-lock.json"
                "${CMAKE_CURRENT_SOURCE_DIR}/tsconfig.json"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Building pocket-agent-host"
            VERBATIM
        )
        add_custom_target(
            pocket_agent_host ALL
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/dist/index.js"
        )
        set_target_properties(pocket_agent_host PROPERTIES FOLDER "tools")
    else()
        message(
            STATUS
            "pocket-agent-host dependencies are not installed; run "
            "npm --prefix tools/pocket-agent-host install"
        )
    endif()
endif()
