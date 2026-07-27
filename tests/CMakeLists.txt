add_executable(
    editor_bridge_protocol_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/editor_bridge_protocol_tests.cpp"
    "${PROJECT_SOURCE_DIR}/src/editor/bridge/EditorBridgeProtocol.cpp"
)
target_include_directories(
    editor_bridge_protocol_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(
    editor_bridge_protocol_tests
    PRIVATE
        game_engine_vendor_headers
)
target_compile_definitions(
    editor_bridge_protocol_tests
    PRIVATE
        POCKET_ENGINE_TEST_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tools/pocket-agent-host/test/fixtures"
)
game_engine_apply_cxx_warnings(editor_bridge_protocol_tests)
set_target_properties(editor_bridge_protocol_tests PROPERTIES FOLDER "tests")
add_test(NAME editor_bridge_protocol COMMAND editor_bridge_protocol_tests)

add_executable(
    ai_editor_service_smoke_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/ai_editor_service_smoke_tests.cpp"
)
target_link_libraries(
    ai_editor_service_smoke_tests
    PRIVATE
        editor_core
        Threads::Threads
)
target_compile_definitions(
    ai_editor_service_smoke_tests
    PRIVATE
        POCKET_ENGINE_TEST_HOST_ENTRY="${PROJECT_SOURCE_DIR}/tools/pocket-agent-host/dist/index.js"
)
game_engine_apply_cxx_warnings(ai_editor_service_smoke_tests)
set_target_properties(ai_editor_service_smoke_tests PROPERTIES FOLDER "tests")
add_test(NAME ai_editor_service_smoke COMMAND ai_editor_service_smoke_tests)
set_tests_properties(ai_editor_service_smoke PROPERTIES SKIP_RETURN_CODE 77)

add_executable(
    ai_read_only_tools_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/ai_read_only_tools_tests.cpp"
)
target_link_libraries(
    ai_read_only_tools_tests
    PRIVATE
        editor_core
)
target_compile_definitions(
    ai_read_only_tools_tests
    PRIVATE
        POCKET_ENGINE_TEST_PROJECT_ROOT="${PROJECT_SOURCE_DIR}/Projects/Default"
)
game_engine_apply_cxx_warnings(ai_read_only_tools_tests)
set_target_properties(ai_read_only_tools_tests PROPERTIES FOLDER "tests")
add_test(NAME ai_read_only_tools COMMAND ai_read_only_tools_tests)
