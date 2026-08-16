add_executable(
    pocket3d_phase1_spec_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/phase1_test_spec_tests.cpp"
)
target_include_directories(
    pocket3d_phase1_spec_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    pocket3d_phase1_spec_tests
    PRIVATE
        game_engine_vendor_headers
)
target_compile_definitions(
    pocket3d_phase1_spec_tests
    PRIVATE
        POCKET3D_PHASE1_TEST_CATALOG="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/phase1-test-catalog.json"
        POCKET3D_PHASE1_TEST_SPEC="${PROJECT_SOURCE_DIR}/docs/development/pocket3d-phase-1-test-spec.md"
        POCKET3D_PHASE1_SPEC_TEST_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/phase1_test_spec_tests.cpp"
        POCKET3D_PHASE1_MATH_TEST_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/phase1_math_spec_tests.cpp"
        POCKET3D_PHASE1_CONFIG_TEST_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/phase1_config_tests.cpp"
        POCKET3D_PHASE1_COMPONENT_TEST_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/phase1_components_tests.cpp"
        POCKET3D_PHASE1_OPENGL_TEST_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/opengl_smoke_tests.cpp"
)
game_engine_apply_cxx_warnings(pocket3d_phase1_spec_tests)
set_target_properties(
    pocket3d_phase1_spec_tests
    PROPERTIES FOLDER "tests/pocket3d"
)
add_test(NAME pocket3d_phase1_spec COMMAND pocket3d_phase1_spec_tests)
set_tests_properties(
    pocket3d_phase1_spec
    PROPERTIES LABELS "pocket3d;phase1;spec;portable;p1.0"
)

add_executable(
    pocket3d_phase1_config_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/phase1_config_tests.cpp"
)
target_link_libraries(pocket3d_phase1_config_tests PRIVATE engine_runtime)
target_compile_definitions(
    pocket3d_phase1_config_tests
    PRIVATE POCKET3D_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
)
game_engine_apply_cxx_warnings(pocket3d_phase1_config_tests)
game_engine_link_sdl(pocket3d_phase1_config_tests)
set_target_properties(pocket3d_phase1_config_tests PROPERTIES FOLDER "tests/pocket3d")
add_test(NAME pocket3d_phase1_config COMMAND pocket3d_phase1_config_tests)
set_tests_properties(
    pocket3d_phase1_config
    PROPERTIES LABELS "pocket3d;phase1;config;portable;p1.0"
)

add_executable(
    pocket3d_phase1_components_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/phase1_components_tests.cpp"
)
target_link_libraries(pocket3d_phase1_components_tests PRIVATE engine_runtime)
game_engine_apply_cxx_warnings(pocket3d_phase1_components_tests)
game_engine_link_sdl(pocket3d_phase1_components_tests)
set_target_properties(pocket3d_phase1_components_tests PROPERTIES FOLDER "tests/pocket3d")
add_test(NAME pocket3d_phase1_components COMMAND pocket3d_phase1_components_tests)
set_tests_properties(
    pocket3d_phase1_components
    PROPERTIES LABELS "pocket3d;phase1;components;portable;p1.2;p1.3"
)

add_executable(
    pocket3d_opengl_smoke_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/opengl_smoke_tests.cpp"
)
target_link_libraries(pocket3d_opengl_smoke_tests PRIVATE engine_runtime)
game_engine_apply_cxx_warnings(pocket3d_opengl_smoke_tests)
game_engine_link_sdl(pocket3d_opengl_smoke_tests)
set_target_properties(pocket3d_opengl_smoke_tests PROPERTIES FOLDER "tests/pocket3d")
add_test(NAME pocket3d_opengl_smoke COMMAND pocket3d_opengl_smoke_tests)
set_tests_properties(
    pocket3d_opengl_smoke
    PROPERTIES
        LABELS "pocket3d;phase1;opengl;graphics;linux-software;p1.1;p1.3;p1.5"
        SKIP_RETURN_CODE 77
)

add_executable(
    pocket3d_phase1_math_spec_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/phase1_math_spec_tests.cpp"
)
target_include_directories(
    pocket3d_phase1_math_spec_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(
    pocket3d_phase1_math_spec_tests
    PRIVATE
        game_engine_vendor_headers
)
game_engine_apply_cxx_warnings(pocket3d_phase1_math_spec_tests)
set_target_properties(
    pocket3d_phase1_math_spec_tests
    PROPERTIES FOLDER "tests/pocket3d"
)
add_test(
    NAME pocket3d_phase1_math_spec
    COMMAND pocket3d_phase1_math_spec_tests
)
set_tests_properties(
    pocket3d_phase1_math_spec
    PROPERTIES LABELS "pocket3d;phase1;spec;math;portable;p1.0"
)
