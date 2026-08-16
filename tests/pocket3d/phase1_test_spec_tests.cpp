#include "TestSupport.h"
#include "rapidjson/document.h"
#include <iostream>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifndef POCKET3D_PHASE1_TEST_CATALOG
#define POCKET3D_PHASE1_TEST_CATALOG ""
#endif

#ifndef POCKET3D_PHASE1_TEST_SPEC
#define POCKET3D_PHASE1_TEST_SPEC ""
#endif

#ifndef POCKET3D_PHASE1_SPEC_TEST_SOURCE
#define POCKET3D_PHASE1_SPEC_TEST_SOURCE ""
#endif

#ifndef POCKET3D_PHASE1_MATH_TEST_SOURCE
#define POCKET3D_PHASE1_MATH_TEST_SOURCE ""
#endif

#ifndef POCKET3D_PHASE1_CONFIG_TEST_SOURCE
#define POCKET3D_PHASE1_CONFIG_TEST_SOURCE ""
#endif

#ifndef POCKET3D_PHASE1_COMPONENT_TEST_SOURCE
#define POCKET3D_PHASE1_COMPONENT_TEST_SOURCE ""
#endif

#ifndef POCKET3D_PHASE1_OPENGL_TEST_SOURCE
#define POCKET3D_PHASE1_OPENGL_TEST_SOURCE ""
#endif

namespace {

const std::vector<std::string> kRequiredCaseIds = {
    "P1-SPEC-001",   "P1-MATH-001",   "P1-MATH-002",
    "P1-MATH-003",   "P1-MATH-004",   "P1-MATH-005",
    "P1-CFG-001",    "P1-CFG-002",    "P1-CFG-003",
    "P1-CFG-004",    "P1-HOST-001",   "P1-HOST-002",
    "P1-HOST-003",   "P1-HOST-004",   "P1-HOST-005",
    "P1-HOST-006",   "P1-TRS-001",    "P1-TRS-002",
    "P1-TRS-003",    "P1-TRS-004",    "P1-PROP-001",
    "P1-PROP-002",   "P1-CAM-001",    "P1-CAM-002",
    "P1-EXT-001",    "P1-EXT-002",    "P1-DRAW-001",
    "P1-DRAW-002",   "P1-DRAW-003",   "P1-DRAW-004",
    "P1-SHADER-001", "P1-RES-001",    "P1-EDIT-001",
    "P1-EDIT-002",   "P1-EDIT-003",   "P1-EDIT-004",
    "P1-EDIT-005",   "P1-EDIT-006",   "P1-EDIT-007",
    "P1-GFX-001",    "P1-GFX-002",    "P1-GFX-003",
    "P1-LIFE-001",   "P1-COMPAT-001", "P1-COMPAT-002",
    "P1-ACCEPT-001"};

bool HasStringMember(const rapidjson::Value &object, const char *name) {
    return object.HasMember(name) && object[name].IsString() &&
           object[name].GetStringLength() > 0;
}

std::string ReadStringMember(const rapidjson::Value &object,
                             const char *name) {
    if (!HasStringMember(object, name)) return "";
    return object[name].GetString();
}

void ExpectCatalogString(Pocket3DTestContext &context,
                         const rapidjson::Document &catalog,
                         const char *name, const char *expected) {
    context.Expect(HasStringMember(catalog, name),
                   std::string("P1-SPEC-001 missing catalog field: ") + name);
    if (!HasStringMember(catalog, name)) return;
    context.Expect(std::string(catalog[name].GetString()) == expected,
                   std::string("P1-SPEC-001 unexpected ") + name);
}

} // namespace

int main() {
    Pocket3DTestContext context;
    const std::string catalog_text =
        ReadPocket3DTestFile(POCKET3D_PHASE1_TEST_CATALOG);
    const std::string spec_text =
        ReadPocket3DTestFile(POCKET3D_PHASE1_TEST_SPEC);
    const std::string spec_test_source =
        ReadPocket3DTestFile(POCKET3D_PHASE1_SPEC_TEST_SOURCE);
    const std::string math_test_source =
        ReadPocket3DTestFile(POCKET3D_PHASE1_MATH_TEST_SOURCE);
    const std::string config_test_source =
        ReadPocket3DTestFile(POCKET3D_PHASE1_CONFIG_TEST_SOURCE);
    const std::string component_test_source =
        ReadPocket3DTestFile(POCKET3D_PHASE1_COMPONENT_TEST_SOURCE);
    const std::string opengl_test_source =
        ReadPocket3DTestFile(POCKET3D_PHASE1_OPENGL_TEST_SOURCE);
    context.Expect(!catalog_text.empty(),
                   "P1-SPEC-001 machine-readable catalog must exist");
    context.Expect(!spec_text.empty(),
                   "P1-SPEC-001 Markdown test spec must exist");
    context.Expect(!spec_test_source.empty(),
                   "P1-SPEC-001 spec runner source must exist");
    context.Expect(!math_test_source.empty(),
                   "P1-SPEC-001 math runner source must exist");
    context.Expect(!config_test_source.empty(),
                   "P1-SPEC-001 config runner source must exist");
    context.Expect(!component_test_source.empty(),
                   "P1-SPEC-001 component runner source must exist");
    context.Expect(!opengl_test_source.empty(),
                   "P1-SPEC-001 OpenGL runner source must exist");
    if (catalog_text.empty() || spec_text.empty() ||
        spec_test_source.empty() || math_test_source.empty() ||
        config_test_source.empty() || component_test_source.empty() ||
        opengl_test_source.empty()) {
        return 1;
    }

    rapidjson::Document catalog;
    catalog.Parse(catalog_text.c_str());
    context.Expect(!catalog.HasParseError() && catalog.IsObject(),
                   "P1-SPEC-001 catalog must be a JSON object");
    if (catalog.HasParseError() || !catalog.IsObject()) return 1;

    context.Expect(catalog.HasMember("schema_version") &&
                       catalog["schema_version"].IsInt() &&
                       catalog["schema_version"].GetInt() == 1,
                   "P1-SPEC-001 schema_version must be 1");
    ExpectCatalogString(context, catalog, "phase", "Pocket3D Phase 1");
    ExpectCatalogString(context, catalog, "spec_version", "2.0");
    ExpectCatalogString(context, catalog, "graphics_api", "OpenGL 4.1 Core");
    ExpectCatalogString(context, catalog, "threading", "main-thread");
    ExpectCatalogString(context, catalog, "script_runtime", "Lua");
    ExpectCatalogString(context, catalog, "physics_3d", "none");

    context.Expect(catalog.HasMember("coordinate_contract") &&
                       catalog["coordinate_contract"].IsObject(),
                   "P1-SPEC-001 coordinate_contract must exist");
    if (catalog.HasMember("coordinate_contract") &&
        catalog["coordinate_contract"].IsObject()) {
        const rapidjson::Value &coordinates = catalog["coordinate_contract"];
        const std::vector<std::pair<const char *, const char *>> expected = {
            {"handedness", "right"},
            {"up_axis", "+Y"},
            {"forward_axis", "-Z"},
            {"front_face", "counter-clockwise"},
            {"ndc_depth", "zero-to-one"},
            {"matrix_layout", "column-major"},
            {"transform_order", "projection * view * world * position"},
            {"uv_origin", "top-left"}};
        for (const auto &entry : expected) {
            context.Expect(HasStringMember(coordinates, entry.first) &&
                               std::string(coordinates[entry.first].GetString()) ==
                                   entry.second,
                           std::string("P1-SPEC-001 coordinate mismatch: ") +
                               entry.first);
        }
    }

    context.Expect(catalog.HasMember("cases") && catalog["cases"].IsArray(),
                   "P1-SPEC-001 cases must be an array");
    if (!catalog.HasMember("cases") || !catalog["cases"].IsArray()) return 1;

    const std::set<std::string> allowed_milestones = {
        "P1.0", "P1.1", "P1.2", "P1.3", "P1.4", "P1.5"};
    const std::set<std::string> allowed_levels = {
        "unit", "integration", "graphics", "manual"};
    const std::set<std::string> allowed_environments = {
        "portable",       "linux-software", "windows-opengl",
        "macos-opengl",   "editor-linux",   "editor-windows",
        "editor-macos"};
    const std::set<std::string> allowed_execution = {
        "active", "planned", "manual"};
    const std::regex id_pattern("^P1-[A-Z]+-[0-9]{3}$");
    std::set<std::string> observed_ids;
    std::set<std::string> observed_milestones;
    int active_case_count = 0;

    for (const rapidjson::Value &test_case : catalog["cases"].GetArray()) {
        context.Expect(test_case.IsObject(),
                       "P1-SPEC-001 every case must be an object");
        if (!test_case.IsObject()) continue;
        const std::vector<const char *> required_fields = {
            "id",          "milestone", "area",  "level",
            "environment", "execution", "runner", "requirement",
            "oracle"};
        for (const char *field : required_fields) {
            context.Expect(HasStringMember(test_case, field),
                           std::string("P1-SPEC-001 missing case field: ") +
                               field);
        }

        const std::string id = ReadStringMember(test_case, "id");
        const std::string milestone =
            ReadStringMember(test_case, "milestone");
        const std::string level = ReadStringMember(test_case, "level");
        const std::string environment =
            ReadStringMember(test_case, "environment");
        const std::string execution =
            ReadStringMember(test_case, "execution");
        const std::string runner = ReadStringMember(test_case, "runner");
        if (id.empty()) continue;

        context.Expect(std::regex_match(id, id_pattern),
                       "P1-SPEC-001 invalid case id: " + id);
        context.Expect(observed_ids.insert(id).second,
                       "P1-SPEC-001 duplicate case id: " + id);
        context.Expect(allowed_milestones.count(milestone) == 1,
                       "P1-SPEC-001 invalid milestone for " + id);
        context.Expect(allowed_levels.count(level) == 1,
                       "P1-SPEC-001 invalid level for " + id);
        context.Expect(allowed_environments.count(environment) == 1,
                       "P1-SPEC-001 invalid environment for " + id);
        context.Expect(allowed_execution.count(execution) == 1,
                       "P1-SPEC-001 invalid execution for " + id);
        context.Expect(spec_text.find("`" + id + "`") != std::string::npos,
                       "P1-SPEC-001 Markdown spec missing " + id);
        observed_milestones.insert(milestone);

        if (execution == "active") {
            ++active_case_count;
            context.Expect(runner == "pocket3d_phase1_spec" ||
                               runner == "pocket3d_phase1_math_spec" ||
                               runner == "pocket3d_rendering_mode_contract" ||
                               runner == "pocket3d_component_contract" ||
                               runner == "pocket3d_opengl_smoke",
                           "P1-SPEC-001 unknown active runner for " + id);
            if (id == "P1-SPEC-001") {
                context.Expect(runner == "pocket3d_phase1_spec",
                               "P1-SPEC-001 must use the spec runner");
            } else if (id.rfind("P1-MATH-", 0) == 0) {
                context.Expect(runner == "pocket3d_phase1_math_spec",
                               "P1-SPEC-001 math case has wrong runner: " + id);
                context.Expect(math_test_source.find(id) != std::string::npos,
                               "P1-SPEC-001 math source missing active case: " +
                                   id);
            } else if (runner == "pocket3d_rendering_mode_contract") {
                context.Expect(config_test_source.find(id) != std::string::npos,
                               "P1-SPEC-001 config source missing active case: " +
                                   id);
            } else if (runner == "pocket3d_component_contract") {
                context.Expect(
                    component_test_source.find(id) != std::string::npos,
                    "P1-SPEC-001 component source missing active case: " + id);
            } else if (runner == "pocket3d_opengl_smoke") {
                context.Expect(opengl_test_source.find(id) != std::string::npos,
                               "P1-SPEC-001 OpenGL source missing active case: " +
                                   id);
            }
        }
        if (execution == "manual") {
            context.Expect(level == "manual",
                           "P1-SPEC-001 manual execution requires manual level: " +
                               id);
        }
    }

    for (const std::string &required_id : kRequiredCaseIds) {
        context.Expect(observed_ids.count(required_id) == 1,
                       "P1-SPEC-001 required case missing: " + required_id);
    }
    for (const std::string &milestone : allowed_milestones) {
        context.Expect(observed_milestones.count(milestone) == 1,
                       "P1-SPEC-001 milestone has no cases: " + milestone);
    }
    context.Expect(active_case_count == 20,
                   "P1-SPEC-001 exactly twenty active cases are expected");

    if (context.FailureCount() != 0) return 1;
    std::cout << "Pocket3D Phase 1 test catalog passed with "
              << observed_ids.size() << " traceable cases." << std::endl;
    return 0;
}
