#ifndef OPENGL_RENDERER_3D_H
#define OPENGL_RENDERER_3D_H

#include "rendering/Render3D.h"
#include <filesystem>
#include <string>

struct SDL_Window;

class OpenGLRenderer3D {
public:
    OpenGLRenderer3D();
    ~OpenGLRenderer3D();

    OpenGLRenderer3D(const OpenGLRenderer3D &) = delete;
    OpenGLRenderer3D &operator=(const OpenGLRenderer3D &) = delete;

    bool Initialize(SDL_Window *window, std::string *out_error = nullptr);
    void Shutdown();
    bool Render(const RenderFrame3D &frame, int width, int height,
                float clear_r, float clear_g, float clear_b,
                std::string *out_error = nullptr);
    bool CaptureBackbufferPPM(const std::filesystem::path &path, int width,
                              int height,
                              std::string *out_error = nullptr) const;
    const std::string &VersionString() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

#endif
