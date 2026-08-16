#include "TestSupport.h"
#include "rendering/OpenGLRenderer3D.h"
#include "SDL2/SDL.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr int kSkipReturnCode = 77;

} // namespace

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cout << "P1-GFX-001 skipped: SDL video unavailable: "
                  << SDL_GetError() << std::endl;
        return kSkipReturnCode;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window *window = SDL_CreateWindow(
        "Pocket3D OpenGL automated smoke", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 320, 180,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        std::cout << "P1-GFX-001 skipped: OpenGL window unavailable: "
                  << SDL_GetError() << std::endl;
        SDL_Quit();
        return kSkipReturnCode;
    }
    SDL_GLContext context_handle = SDL_GL_CreateContext(window);
    if (context_handle == nullptr) {
        std::cout << "P1-GFX-001 skipped: OpenGL 4.1 context unavailable: "
                  << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return kSkipReturnCode;
    }

    Pocket3DTestContext context;
    OpenGLRenderer3D renderer;
    std::string error;
    context.Expect(renderer.Initialize(window, &error),
                   "P1-HOST-001 OpenGL renderer and P1-SHADER-001 GLSL 410 "
                   "program initialize: " + error);

    RenderFrame3D frame;
    frame.view.view = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -4));
    frame.view.projection = glm::perspectiveRH_ZO(
        glm::radians(55.0f), 320.0f / 180.0f, 0.1f, 100.0f);
    MeshDrawCommand3D far_cube;
    far_cube.actor_uid = 1;
    far_cube.world = glm::translate(glm::mat4(1.0f), glm::vec3(0.25f, 0, -0.8f));
    far_cube.color = glm::vec4(0.15f, 0.8f, 0.35f, 1.0f);
    MeshDrawCommand3D near_cube;
    near_cube.actor_uid = 2;
    near_cube.world = glm::translate(glm::mat4(1.0f), glm::vec3(-0.2f, 0, 0));
    near_cube.color = glm::vec4(0.2f, 0.45f, 1.0f, 1.0f);
    frame.draws.push_back(near_cube);
    // Submit the farther green cube last. The overlap can remain blue only if
    // depth testing/writes are effective; painter's order would turn it green.
    frame.draws.push_back(far_cube);

    context.Expect(renderer.Render(frame, 320, 180, 0.04f, 0.06f, 0.1f,
                                   &error),
                   "P1-DRAW-001 indexed depth draw succeeds: " + error);
    const std::filesystem::path capture_path =
        std::filesystem::temp_directory_path() /
        "pocket3d-opengl-smoke.ppm";
    context.Expect(renderer.CaptureBackbufferPPM(capture_path, 320, 180,
                                                 &error),
                   "P1-GFX-001 backbuffer readback succeeds: " + error);

    std::ifstream capture(capture_path, std::ios::binary);
    std::string magic;
    int width = 0;
    int height = 0;
    int max_value = 0;
    capture >> magic >> width >> height >> max_value;
    capture.get();
    std::vector<std::uint8_t> pixels{
        std::istreambuf_iterator<char>(capture),
        std::istreambuf_iterator<char>()};
    context.Expect(magic == "P6" && width == 320 && height == 180 &&
                       max_value == 255,
                   "P1-GFX-001 readback header is deterministic");
    context.Expect(pixels.size() == 320U * 180U * 3U,
                   "P1-GFX-001 readback byte count is deterministic");
    std::size_t colored_pixels = 0;
    for (std::size_t index = 0; index + 2 < pixels.size(); index += 3) {
        if (pixels[index] > 20 || pixels[index + 1] > 25 ||
            pixels[index + 2] > 35) {
            ++colored_pixels;
        }
    }
    context.Expect(colored_pixels > 500,
                   "P1-DRAW-001 visible geometry covers expected pixels");
    if (pixels.size() == 320U * 180U * 3U) {
        const std::size_t center = (90U * 320U + 160U) * 3U;
        context.Expect(pixels[center + 2] > pixels[center + 1],
                       "P1-DRAW-001 near blue cube occludes farther green cube");
    }

    std::error_code remove_error;
    std::filesystem::remove(capture_path, remove_error);
    renderer.Shutdown();
    SDL_GL_DeleteContext(context_handle);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (context.FailureCount() != 0) return 1;
    std::cout << "Pocket3D OpenGL 4.1 hidden-window render/readback passed."
              << std::endl;
    return 0;
}
