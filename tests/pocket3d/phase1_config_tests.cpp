#include "TestSupport.h"
#include "shared/config/GameConfig.h"
#include "shared/resources/ResourcePath.h"
#include <iostream>

#ifndef POCKET3D_SOURCE_ROOT
#define POCKET3D_SOURCE_ROOT "."
#endif

int main() {
    Pocket3DTestContext context;
    RenderingMode mode = RenderingMode::TwoD;
    context.Expect(GameConfig::TryParseRenderingMode("2d", mode) &&
                       mode == RenderingMode::TwoD,
                   "P1-CFG-002 parses 2d");
    context.Expect(GameConfig::TryParseRenderingMode("3d", mode) &&
                       mode == RenderingMode::ThreeD,
                   "P1-CFG-002 parses 3d");
    context.Expect(!GameConfig::TryParseRenderingMode("vulkan", mode),
                   "P1-CFG-002 rejects unknown rendering mode");

    ResourcePath::SetResourcesRootPath(
        std::filesystem::path(POCKET3D_SOURCE_ROOT) / "Projects" / "Default");
    const GameConfigData legacy_config = GameConfig::Read();
    context.Expect(legacy_config.valid &&
                       legacy_config.rendering_mode == RenderingMode::TwoD,
                   "P1-CFG-001 missing rendering_mode preserves 2D");

    ResourcePath::SetResourcesRootPath(
        std::filesystem::path(POCKET3D_SOURCE_ROOT) / "Projects" / "Pocket3D");
    const GameConfigData opengl_config = GameConfig::Read();
    context.Expect(opengl_config.valid &&
                       opengl_config.rendering_mode == RenderingMode::ThreeD,
                   "P1-CFG-003 3d selects cross-platform OpenGL host");
    context.Expect(!opengl_config.vsync,
                   "P1-CFG-003 project vsync value is preserved");

    ResourcePath::SetResourcesRootPath(ResourcePath::DefaultResourcesRoot());
    if (context.FailureCount() != 0) return 1;
    std::cout << "Pocket3D rendering_mode configuration contracts passed."
              << std::endl;
    return 0;
}
