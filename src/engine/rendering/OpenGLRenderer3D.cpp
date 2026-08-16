#include "rendering/OpenGLRenderer3D.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_opengl.h"
#include "SDL2/SDL_opengl_glext.h"
#include "glm/gtc/type_ptr.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

template <typename FunctionType>
bool LoadGLFunction(FunctionType &out_function, const char *name,
                    std::string &out_error) {
    out_function = reinterpret_cast<FunctionType>(SDL_GL_GetProcAddress(name));
    if (out_function != nullptr) return true;
    out_error = std::string("opengl.missing_function:") + name;
    return false;
}

std::string ShaderInfoLog(GLuint shader, PFNGLGETSHADERIVPROC get_iv,
                          PFNGLGETSHADERINFOLOGPROC get_log) {
    GLint length = 0;
    get_iv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) return "unknown shader compile error";
    std::vector<GLchar> log(static_cast<std::size_t>(length));
    get_log(shader, length, nullptr, log.data());
    return std::string(log.data());
}

std::string ProgramInfoLog(GLuint program, PFNGLGETPROGRAMIVPROC get_iv,
                           PFNGLGETPROGRAMINFOLOGPROC get_log) {
    GLint length = 0;
    get_iv(program, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) return "unknown program link error";
    std::vector<GLchar> log(static_cast<std::size_t>(length));
    get_log(program, length, nullptr, log.data());
    return std::string(log.data());
}

} // namespace

struct OpenGLRenderer3D::Impl {
    using GetStringProc = const GLubyte *(APIENTRYP)(GLenum);
    using GetIntegervProc = void(APIENTRYP)(GLenum, GLint *);
    using ViewportProc = void(APIENTRYP)(GLint, GLint, GLsizei, GLsizei);
    using ClearColorProc = void(APIENTRYP)(GLfloat, GLfloat, GLfloat, GLfloat);
    using ClearProc = void(APIENTRYP)(GLbitfield);
    using EnableProc = void(APIENTRYP)(GLenum);
    using DepthFuncProc = void(APIENTRYP)(GLenum);
    using CullFaceProc = void(APIENTRYP)(GLenum);
    using FrontFaceProc = void(APIENTRYP)(GLenum);
    using ReadPixelsProc = void(APIENTRYP)(GLint, GLint, GLsizei, GLsizei,
                                           GLenum, GLenum, void *);
    using PixelStoreiProc = void(APIENTRYP)(GLenum, GLint);
    using FinishProc = void(APIENTRYP)();
    using DrawElementsProc = void(APIENTRYP)(GLenum, GLsizei, GLenum,
                                              const void *);

    SDL_Window *window = nullptr;
    std::string version_string;
    GLuint vertex_array = 0;
    GLuint vertex_buffer = 0;
    GLuint index_buffer = 0;
    GLuint program = 0;
    GLint mvp_location = -1;
    GLint model_location = -1;
    GLint color_location = -1;

    GetStringProc get_string = nullptr;
    GetIntegervProc get_integerv = nullptr;
    ViewportProc viewport = nullptr;
    ClearColorProc clear_color = nullptr;
    ClearProc clear = nullptr;
    EnableProc enable = nullptr;
    DepthFuncProc depth_func = nullptr;
    CullFaceProc cull_face = nullptr;
    FrontFaceProc front_face = nullptr;
    ReadPixelsProc read_pixels = nullptr;
    PixelStoreiProc pixel_store_i = nullptr;
    FinishProc finish = nullptr;
    PFNGLGENVERTEXARRAYSPROC gen_vertex_arrays = nullptr;
    PFNGLBINDVERTEXARRAYPROC bind_vertex_array = nullptr;
    PFNGLDELETEVERTEXARRAYSPROC delete_vertex_arrays = nullptr;
    PFNGLGENBUFFERSPROC gen_buffers = nullptr;
    PFNGLBINDBUFFERPROC bind_buffer = nullptr;
    PFNGLBUFFERDATAPROC buffer_data = nullptr;
    PFNGLDELETEBUFFERSPROC delete_buffers = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC enable_vertex_attrib_array = nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC vertex_attrib_pointer = nullptr;
    PFNGLCREATESHADERPROC create_shader = nullptr;
    PFNGLSHADERSOURCEPROC shader_source = nullptr;
    PFNGLCOMPILESHADERPROC compile_shader = nullptr;
    PFNGLGETSHADERIVPROC get_shader_iv = nullptr;
    PFNGLGETSHADERINFOLOGPROC get_shader_info_log = nullptr;
    PFNGLDELETESHADERPROC delete_shader = nullptr;
    PFNGLCREATEPROGRAMPROC create_program = nullptr;
    PFNGLATTACHSHADERPROC attach_shader = nullptr;
    PFNGLLINKPROGRAMPROC link_program = nullptr;
    PFNGLGETPROGRAMIVPROC get_program_iv = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC get_program_info_log = nullptr;
    PFNGLDELETEPROGRAMPROC delete_program = nullptr;
    PFNGLUSEPROGRAMPROC use_program = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC get_uniform_location = nullptr;
    PFNGLUNIFORMMATRIX4FVPROC uniform_matrix_4fv = nullptr;
    PFNGLUNIFORM4FPROC uniform_4f = nullptr;
    DrawElementsProc draw_elements = nullptr;

    bool LoadFunctions(std::string &out_error) {
#define LOAD_GL(member, symbol)                                                \
    if (!LoadGLFunction(member, symbol, out_error)) return false
        LOAD_GL(get_string, "glGetString");
        LOAD_GL(get_integerv, "glGetIntegerv");
        LOAD_GL(viewport, "glViewport");
        LOAD_GL(clear_color, "glClearColor");
        LOAD_GL(clear, "glClear");
        LOAD_GL(enable, "glEnable");
        LOAD_GL(depth_func, "glDepthFunc");
        LOAD_GL(cull_face, "glCullFace");
        LOAD_GL(front_face, "glFrontFace");
        LOAD_GL(read_pixels, "glReadPixels");
        LOAD_GL(pixel_store_i, "glPixelStorei");
        LOAD_GL(finish, "glFinish");
        LOAD_GL(gen_vertex_arrays, "glGenVertexArrays");
        LOAD_GL(bind_vertex_array, "glBindVertexArray");
        LOAD_GL(delete_vertex_arrays, "glDeleteVertexArrays");
        LOAD_GL(gen_buffers, "glGenBuffers");
        LOAD_GL(bind_buffer, "glBindBuffer");
        LOAD_GL(buffer_data, "glBufferData");
        LOAD_GL(delete_buffers, "glDeleteBuffers");
        LOAD_GL(enable_vertex_attrib_array, "glEnableVertexAttribArray");
        LOAD_GL(vertex_attrib_pointer, "glVertexAttribPointer");
        LOAD_GL(create_shader, "glCreateShader");
        LOAD_GL(shader_source, "glShaderSource");
        LOAD_GL(compile_shader, "glCompileShader");
        LOAD_GL(get_shader_iv, "glGetShaderiv");
        LOAD_GL(get_shader_info_log, "glGetShaderInfoLog");
        LOAD_GL(delete_shader, "glDeleteShader");
        LOAD_GL(create_program, "glCreateProgram");
        LOAD_GL(attach_shader, "glAttachShader");
        LOAD_GL(link_program, "glLinkProgram");
        LOAD_GL(get_program_iv, "glGetProgramiv");
        LOAD_GL(get_program_info_log, "glGetProgramInfoLog");
        LOAD_GL(delete_program, "glDeleteProgram");
        LOAD_GL(use_program, "glUseProgram");
        LOAD_GL(get_uniform_location, "glGetUniformLocation");
        LOAD_GL(uniform_matrix_4fv, "glUniformMatrix4fv");
        LOAD_GL(uniform_4f, "glUniform4f");
        LOAD_GL(draw_elements, "glDrawElements");
#undef LOAD_GL
        return true;
    }

    GLuint CompileShader(GLenum type, const char *source,
                         std::string &out_error) {
        const GLuint shader = create_shader(type);
        shader_source(shader, 1, &source, nullptr);
        compile_shader(shader);
        GLint compiled = GL_FALSE;
        get_shader_iv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_TRUE) return shader;
        out_error = "opengl.shader_compile:" +
                    ShaderInfoLog(shader, get_shader_iv, get_shader_info_log);
        delete_shader(shader);
        return 0;
    }

    bool CreateProgram(std::string &out_error) {
        static constexpr const char *kVertexShader = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMvp;
uniform mat4 uModel;
out vec3 vWorldNormal;
void main() {
    vec4 clip = uMvp * vec4(aPosition, 1.0);
    clip.z = 2.0 * clip.z - clip.w;
    gl_Position = clip;
    vWorldNormal = normalize(mat3(uModel) * aNormal);
}
)GLSL";
        static constexpr const char *kFragmentShader = R"GLSL(
#version 410 core
in vec3 vWorldNormal;
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    vec3 lightDirection = normalize(vec3(0.45, 0.8, 0.35));
    float diffuse = max(dot(normalize(vWorldNormal), lightDirection), 0.0);
    float lighting = 0.28 + 0.72 * diffuse;
    fragColor = vec4(uColor.rgb * lighting, uColor.a);
}
)GLSL";

        const GLuint vertex_shader =
            CompileShader(GL_VERTEX_SHADER, kVertexShader, out_error);
        if (vertex_shader == 0) return false;
        const GLuint fragment_shader =
            CompileShader(GL_FRAGMENT_SHADER, kFragmentShader, out_error);
        if (fragment_shader == 0) {
            delete_shader(vertex_shader);
            return false;
        }

        program = create_program();
        attach_shader(program, vertex_shader);
        attach_shader(program, fragment_shader);
        link_program(program);
        delete_shader(vertex_shader);
        delete_shader(fragment_shader);
        GLint linked = GL_FALSE;
        get_program_iv(program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            out_error = "opengl.program_link:" + ProgramInfoLog(
                program, get_program_iv, get_program_info_log);
            delete_program(program);
            program = 0;
            return false;
        }
        mvp_location = get_uniform_location(program, "uMvp");
        model_location = get_uniform_location(program, "uModel");
        color_location = get_uniform_location(program, "uColor");
        if (mvp_location < 0 || model_location < 0 || color_location < 0) {
            out_error = "opengl.required_uniform_missing";
            return false;
        }
        return true;
    }

    bool CreateCube(std::string &out_error) {
        // Position and outward normal, four vertices per face.
        static constexpr std::array<float, 24 * 6> kVertices = {{
            -0.5f, -0.5f,  0.5f,  0,  0,  1,  0.5f, -0.5f,  0.5f,  0,  0,  1,
             0.5f,  0.5f,  0.5f,  0,  0,  1, -0.5f,  0.5f,  0.5f,  0,  0,  1,
             0.5f, -0.5f, -0.5f,  0,  0, -1, -0.5f, -0.5f, -0.5f,  0,  0, -1,
            -0.5f,  0.5f, -0.5f,  0,  0, -1,  0.5f,  0.5f, -0.5f,  0,  0, -1,
            -0.5f, -0.5f, -0.5f, -1,  0,  0, -0.5f, -0.5f,  0.5f, -1,  0,  0,
            -0.5f,  0.5f,  0.5f, -1,  0,  0, -0.5f,  0.5f, -0.5f, -1,  0,  0,
             0.5f, -0.5f,  0.5f,  1,  0,  0,  0.5f, -0.5f, -0.5f,  1,  0,  0,
             0.5f,  0.5f, -0.5f,  1,  0,  0,  0.5f,  0.5f,  0.5f,  1,  0,  0,
            -0.5f,  0.5f,  0.5f,  0,  1,  0,  0.5f,  0.5f,  0.5f,  0,  1,  0,
             0.5f,  0.5f, -0.5f,  0,  1,  0, -0.5f,  0.5f, -0.5f,  0,  1,  0,
            -0.5f, -0.5f, -0.5f,  0, -1,  0,  0.5f, -0.5f, -0.5f,  0, -1,  0,
             0.5f, -0.5f,  0.5f,  0, -1,  0, -0.5f, -0.5f,  0.5f,  0, -1,  0,
        }};
        static constexpr std::array<std::uint32_t, 36> kIndices = {{
             0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,
             8,  9, 10,  8, 10, 11, 12, 13, 14, 12, 14, 15,
            16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
        }};

        gen_vertex_arrays(1, &vertex_array);
        gen_buffers(1, &vertex_buffer);
        gen_buffers(1, &index_buffer);
        if (vertex_array == 0 || vertex_buffer == 0 || index_buffer == 0) {
            out_error = "opengl.cube_buffer_creation_failed";
            return false;
        }
        bind_vertex_array(vertex_array);
        bind_buffer(GL_ARRAY_BUFFER, vertex_buffer);
        buffer_data(GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(sizeof(kVertices)),
                    kVertices.data(), GL_STATIC_DRAW);
        bind_buffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
        buffer_data(GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(sizeof(kIndices)), kIndices.data(),
                    GL_STATIC_DRAW);
        enable_vertex_attrib_array(0);
        vertex_attrib_pointer(0, 3, GL_FLOAT, GL_FALSE,
                              static_cast<GLsizei>(6 * sizeof(float)), nullptr);
        enable_vertex_attrib_array(1);
        vertex_attrib_pointer(
            1, 3, GL_FLOAT, GL_FALSE,
            static_cast<GLsizei>(6 * sizeof(float)),
            reinterpret_cast<const void *>(3 * sizeof(float)));
        bind_vertex_array(0);
        return true;
    }
};

OpenGLRenderer3D::OpenGLRenderer3D() : impl_(new Impl()) {}

OpenGLRenderer3D::~OpenGLRenderer3D() {
    Shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool OpenGLRenderer3D::Initialize(SDL_Window *window, std::string *out_error) {
    if (impl_ == nullptr || window == nullptr) {
        if (out_error != nullptr) *out_error = "opengl.invalid_window";
        return false;
    }
    impl_->window = window;
    std::string error;
    if (!impl_->LoadFunctions(error)) {
        if (out_error != nullptr) *out_error = error;
        return false;
    }

    GLint major = 0;
    GLint minor = 0;
    GLint profile_mask = 0;
    impl_->get_integerv(GL_MAJOR_VERSION, &major);
    impl_->get_integerv(GL_MINOR_VERSION, &minor);
    impl_->get_integerv(GL_CONTEXT_PROFILE_MASK, &profile_mask);
    const GLubyte *version = impl_->get_string(GL_VERSION);
    impl_->version_string = version == nullptr
                                ? "unknown"
                                : reinterpret_cast<const char *>(version);
    if (major < 4 || (major == 4 && minor < 1) ||
        (profile_mask & GL_CONTEXT_CORE_PROFILE_BIT) == 0) {
        std::ostringstream message;
        message << "opengl.unsupported_context:" << major << "." << minor;
        if (out_error != nullptr) *out_error = message.str();
        return false;
    }
    if (!impl_->CreateProgram(error) || !impl_->CreateCube(error)) {
        if (out_error != nullptr) *out_error = error;
        return false;
    }

    impl_->enable(GL_DEPTH_TEST);
    impl_->depth_func(GL_LESS);
    impl_->enable(GL_CULL_FACE);
    impl_->cull_face(GL_BACK);
    impl_->front_face(GL_CCW);
    if (out_error != nullptr) out_error->clear();
    return true;
}

void OpenGLRenderer3D::Shutdown() {
    if (impl_ == nullptr) return;
    if (impl_->delete_buffers != nullptr) {
        if (impl_->index_buffer != 0) {
            impl_->delete_buffers(1, &impl_->index_buffer);
            impl_->index_buffer = 0;
        }
        if (impl_->vertex_buffer != 0) {
            impl_->delete_buffers(1, &impl_->vertex_buffer);
            impl_->vertex_buffer = 0;
        }
    }
    if (impl_->delete_vertex_arrays != nullptr && impl_->vertex_array != 0) {
        impl_->delete_vertex_arrays(1, &impl_->vertex_array);
        impl_->vertex_array = 0;
    }
    if (impl_->delete_program != nullptr && impl_->program != 0) {
        impl_->delete_program(impl_->program);
        impl_->program = 0;
    }
    impl_->window = nullptr;
}

bool OpenGLRenderer3D::Render(const RenderFrame3D &frame, int width,
                              int height, float clear_r, float clear_g,
                              float clear_b, std::string *out_error) {
    if (impl_ == nullptr || impl_->program == 0 || width <= 0 || height <= 0) {
        if (out_error != nullptr) *out_error = "opengl.render_not_ready";
        return false;
    }
    impl_->viewport(0, 0, width, height);
    impl_->clear_color(std::clamp(clear_r, 0.0f, 1.0f),
                       std::clamp(clear_g, 0.0f, 1.0f),
                       std::clamp(clear_b, 0.0f, 1.0f), 1.0f);
    impl_->clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    impl_->use_program(impl_->program);
    impl_->bind_vertex_array(impl_->vertex_array);
    for (const MeshDrawCommand3D &draw : frame.draws) {
        // Unknown assets use the visible built-in cube placeholder in Phase 1.
        const glm::mat4 mvp = frame.view.projection * frame.view.view * draw.world;
        impl_->uniform_matrix_4fv(impl_->mvp_location, 1, GL_FALSE,
                                  glm::value_ptr(mvp));
        impl_->uniform_matrix_4fv(impl_->model_location, 1, GL_FALSE,
                                  glm::value_ptr(draw.world));
        impl_->uniform_4f(impl_->color_location, draw.color.r, draw.color.g,
                         draw.color.b, draw.color.a);
        impl_->draw_elements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
    }
    impl_->bind_vertex_array(0);
    if (out_error != nullptr) out_error->clear();
    return true;
}

bool OpenGLRenderer3D::CaptureBackbufferPPM(
    const std::filesystem::path &path, int width, int height,
    std::string *out_error) const {
    if (impl_ == nullptr || impl_->read_pixels == nullptr || width <= 0 ||
        height <= 0) {
        if (out_error != nullptr) *out_error = "opengl.capture_not_ready";
        return false;
    }
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    impl_->pixel_store_i(GL_PACK_ALIGNMENT, 1);
    impl_->finish();
    impl_->read_pixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                       pixels.data());

    std::error_code directory_error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), directory_error);
        if (directory_error) {
            if (out_error != nullptr) {
                *out_error = "opengl.capture_directory_failed";
            }
            return false;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        if (out_error != nullptr) *out_error = "opengl.capture_open_failed";
        return false;
    }
    output << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) *
                4;
            output.write(reinterpret_cast<const char *>(&pixels[index]), 3);
        }
    }
    if (!output.good()) {
        if (out_error != nullptr) *out_error = "opengl.capture_write_failed";
        return false;
    }
    if (out_error != nullptr) out_error->clear();
    return true;
}

const std::string &OpenGLRenderer3D::VersionString() const {
    static const std::string empty;
    return impl_ == nullptr ? empty : impl_->version_string;
}
