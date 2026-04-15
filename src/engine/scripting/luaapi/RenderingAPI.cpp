#include "RegistrationDetail.h"
#include "rendering/Renderer.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"
#include <algorithm>
#include <string>

namespace {

using APIRegistrationDetail::g_lua_state;

int DowncastToInt(float value) {
    return static_cast<int>(value);
}

int ClampByteInt(int value) {
    return std::clamp(value, 0, 255);
}

void CppImageDrawUI(const std::string &image_name, float x, float y) {
    ImageDrawRequest request;
    request.image_name = image_name;
    request.x = static_cast<float>(DowncastToInt(x));
    request.y = static_cast<float>(DowncastToInt(y));
    request.rotation_degrees = 0;
    request.scale_x = 1.0f;
    request.scale_y = 1.0f;
    request.pivot_x = 0.0f;
    request.pivot_y = 0.0f;
    request.r = 255;
    request.g = 255;
    request.b = 255;
    request.a = 255;
    request.sorting_order = 0;
    Renderer::QueueUIImageDraw(request);
}

void CppImageDrawUIEx(const std::string &image_name, float x, float y, float r,
                      float g, float b, float a, float sorting_order) {
    ImageDrawRequest request;
    request.image_name = image_name;
    request.x = static_cast<float>(DowncastToInt(x));
    request.y = static_cast<float>(DowncastToInt(y));
    request.rotation_degrees = 0;
    request.scale_x = 1.0f;
    request.scale_y = 1.0f;
    request.pivot_x = 0.0f;
    request.pivot_y = 0.0f;
    request.r = ClampByteInt(DowncastToInt(r));
    request.g = ClampByteInt(DowncastToInt(g));
    request.b = ClampByteInt(DowncastToInt(b));
    request.a = ClampByteInt(DowncastToInt(a));
    request.sorting_order = DowncastToInt(sorting_order);
    Renderer::QueueUIImageDraw(request);
}

void CppImageDraw(const std::string &image_name, float x, float y) {
    ImageDrawRequest request;
    request.image_name = image_name;
    request.x = x;
    request.y = y;
    request.rotation_degrees = 0;
    request.scale_x = 1.0f;
    request.scale_y = 1.0f;
    request.pivot_x = 0.5f;
    request.pivot_y = 0.5f;
    request.r = 255;
    request.g = 255;
    request.b = 255;
    request.a = 255;
    request.sorting_order = 0;
    Renderer::QueueSceneImageDraw(request);
}

void CppImageDrawEx(const std::string &image_name, float x, float y,
                    float rotation_degrees, float scale_x, float scale_y,
                    float pivot_x, float pivot_y, float r, float g, float b,
                    float a, float sorting_order) {
    ImageDrawRequest request;
    request.image_name = image_name;
    request.x = x;
    request.y = y;
    request.rotation_degrees = rotation_degrees;
    request.scale_x = scale_x;
    request.scale_y = scale_y;
    request.pivot_x = pivot_x;
    request.pivot_y = pivot_y;
    request.r = ClampByteInt(DowncastToInt(r));
    request.g = ClampByteInt(DowncastToInt(g));
    request.b = ClampByteInt(DowncastToInt(b));
    request.a = ClampByteInt(DowncastToInt(a));
    request.sorting_order = DowncastToInt(sorting_order);
    Renderer::QueueSceneImageDraw(request);
}

void CppImageDrawPixel(float x, float y, float r, float g, float b, float a) {
    PixelDrawRequest request;
    request.x = DowncastToInt(x);
    request.y = DowncastToInt(y);
    request.r = ClampByteInt(DowncastToInt(r));
    request.g = ClampByteInt(DowncastToInt(g));
    request.b = ClampByteInt(DowncastToInt(b));
    request.a = ClampByteInt(DowncastToInt(a));
    Renderer::QueuePixelDraw(request);
}

void InjectImageAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginNamespace("Image")
        .addFunction("DrawUI", &CppImageDrawUI)
        .addFunction("DrawUIEx", &CppImageDrawUIEx)
        .addFunction("Draw", &CppImageDraw)
        .addFunction("DrawEx", &CppImageDrawEx)
        .addFunction("DrawPixel", &CppImageDrawPixel)
        .endNamespace();
}

void CppTextDraw(const std::string &str_content, float x, float y,
                 const std::string &font_name, float font_size, float r, float g,
                 float b, float a) {
    const int x_i = static_cast<int>(x);
    const int y_i = static_cast<int>(y);
    const int font_size_i = static_cast<int>(font_size);
    const int r_i = static_cast<int>(r);
    const int g_i = static_cast<int>(g);
    const int b_i = static_cast<int>(b);
    const int a_i = static_cast<int>(a);

    SDL_Color color = {static_cast<Uint8>(r_i), static_cast<Uint8>(g_i),
                       static_cast<Uint8>(b_i), static_cast<Uint8>(a_i)};
    Renderer::DrawText(str_content, font_name, font_size_i, color, x_i, y_i);
}

void InjectTextAPI() {
    luabridge::getGlobalNamespace(g_lua_state)
        .beginNamespace("Text")
        .addFunction("Draw", &CppTextDraw)
        .endNamespace();
}

} // namespace

namespace APIRegistrationDetail {

void RegisterRenderingAPI() {
    InjectImageAPI();
    InjectTextAPI();
}

} // namespace APIRegistrationDetail
