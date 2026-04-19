#include "rendering/Renderer.h"
#include "core/Engine.h"
#include "rendering/SDLRenderHelper.h"
#include "scripting/ComponentManager.h"
#include "shared/resources/ResourcePath.h"
#include "particles/ParticleManager.h"
#include "scene/Scene.h"
#include "glm/glm.hpp"
#include "SDL2_image/SDL_image.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace {

constexpr float kPixelsPerUnit = 100.0f;
constexpr Uint32 kTextCacheUnusedLifetimeMs = 3000;
constexpr std::size_t kMaxTextCacheEntries = 512;

std::string ResolveFontPath(const std::string &font_name) {
    return ResourcePath::ResolveResourcePath(
        "resources/fonts", font_name, {".ttf"},
        Scene::GetActiveSceneSubdirectory());
}

std::string ResolveImagePath(const std::string &image_name) {
    return ResourcePath::ResolveResourcePath(
        "resources/images", image_name, {".png", ".jpg"},
        Scene::GetActiveSceneSubdirectory());
}

int ClampByte(int value) {
    return std::clamp(value, 0, 255);
}

bool CompareImageRequests(const ImageDrawRequest &a,
                          const ImageDrawRequest &b) {
    return a.sorting_order < b.sorting_order;
}

bool CompareParticleBatchRequests(const ParticleBatchRequest &a,
                                  const ParticleBatchRequest &b) {
    return a.sorting_order < b.sorting_order;
}

bool ShouldDiscardSceneRequest(const ImageDrawRequest &request) {
    if (request.a <= 0) return true;
    if (request.scale_x == 0.0f) return true;
    if (request.scale_y == 0.0f) return true;
    return false;
}

bool ShouldDiscardUIRequest(const ImageDrawRequest &request) {
    return request.a <= 0;
}

bool IsPivotBoundOutsideViewport(const SDL_FRect &dst, const SDL_FPoint &pivot,
                                 float viewport_width,
                                 float viewport_height) {
    const float left = pivot.x;
    const float right = dst.w - pivot.x;
    const float top = pivot.y;
    const float bottom = dst.h - pivot.y;
    const float max_dx = std::max(left, right);
    const float max_dy = std::max(top, bottom);
    const float radius = std::sqrt(max_dx * max_dx + max_dy * max_dy);
    const float pivot_x = dst.x + pivot.x;
    const float pivot_y = dst.y + pivot.y;

    return pivot_x + radius <= 0.0f || pivot_y + radius <= 0.0f ||
           pivot_x - radius >= viewport_width ||
           pivot_y - radius >= viewport_height;
}

void DestroyCachedTextTexture(CachedTextTexture &cached) {
    if (cached.texture != nullptr) {
        SDL_DestroyTexture(cached.texture);
        cached.texture = nullptr;
    }
}

SDL_Texture *CreateDefaultParticleTexture(SDL_Renderer *renderer) {
    if (renderer == nullptr) return nullptr;

    // Create an SDL_Surface (a cpu-side texture) with no special flags, 8 width, 8 height, 32 bits of color depth (RGBA) and no masking.
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_RGBA8888);
    if (surface == nullptr) return nullptr;

    // Ensure color set to white (255, 255, 255, 255)
    const Uint32 white_color = SDL_MapRGBA(surface->format, 255, 255, 255, 255);
    SDL_FillRect(surface, nullptr, white_color);

    // Create a gpu-side texture from the cpu-side surface now that we're done editing it.
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);

    // Clean up the surface and cache this default texture for future use (we'll probably spawn many particles with it).
    SDL_FreeSurface(surface);
    if (texture != nullptr) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    return texture;
}

} // namespace

void Renderer::PruneTextCache() {
    const Uint32 now = SDL_GetTicks();

    for (auto it = text_cache.begin(); it != text_cache.end();) {
        CachedTextTexture &cached = it->second;
        const Uint32 age = now - cached.last_used_timestamp;
        if (cached.last_used_timestamp != 0 &&
            age > kTextCacheUnusedLifetimeMs) {
            DestroyCachedTextTexture(cached);
            it = text_cache.erase(it);
            continue;
        }
        ++it;
    }

    while (text_cache.size() > kMaxTextCacheEntries) {
        auto oldest_it = text_cache.end();
        for (auto it = text_cache.begin(); it != text_cache.end(); ++it) {
            if (oldest_it == text_cache.end() ||
                it->second.last_used_timestamp <
                    oldest_it->second.last_used_timestamp) {
                oldest_it = it;
            }
        }
        if (oldest_it == text_cache.end()) {
            break;
        }
        DestroyCachedTextTexture(oldest_it->second);
        text_cache.erase(oldest_it);
    }
}

// Initialize shared rendering backends and per-session caches.
void Renderer::Init() {
    if (initialized) return;
    if (TTF_Init() != 0) {
        exit(0);
    }
    initialized = true;
}

// Release cached fonts, textures, and queued draw state for the session.
void Renderer::Shutdown() {
    for (auto &font_family : font_cache) {
        for (auto &entry : font_family.second) {
            if (entry.second != nullptr) {
                TTF_CloseFont(entry.second);
            }
        }
    }
    font_cache.clear();

    for (auto &entry : text_cache) {
        if (entry.second.texture != nullptr) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    text_cache.clear();
    text_draw_requests.clear();

    ClearCache();

    if (initialized) {
        TTF_Quit();
        initialized = false;
    }
}

// Resolve and cache one font family/size pair for later text rendering.
TTF_Font *Renderer::GetFont(const std::string &font_name, int size) {
    if (font_name.empty()) return nullptr;
    auto family_it = font_cache.find(font_name);
    if (family_it != font_cache.end()) {
        auto size_it = family_it->second.find(size);
        if (size_it != family_it->second.end()) return size_it->second;
    }

    const std::string path = ResolveFontPath(font_name);
    if (path.empty()) {
        std::cout << "error: font " << font_name << " missing";
        exit(0);
    }

    TTF_Font *font = TTF_OpenFont(path.c_str(), size);
    if (font == nullptr) {
        std::cout << "error: font " << font_name << " missing";
        exit(0);
    }
    font_cache[font_name][size] = font;
    return font;
}

// resource loading and draw queue entry points
SDL_Texture *Renderer::LoadTexture(const std::string &image_name,
                                   SDL_Renderer *renderer) {
    auto it = image_cache.find(image_name);
    if (it != image_cache.end()) {
        return it->second;
    }

    if (image_name == kDefaultParticleTextureName) {
        SDL_Texture *texture = CreateDefaultParticleTexture(renderer);
        if (texture == nullptr) {
            std::cout << "error: failed to create default particle texture";
            exit(0);
        }
        image_cache[image_name] = texture;
        return texture;
    }

    std::string image_path = ResolveImagePath(image_name);
    if (image_path.empty()) {
        std::cout << "error: missing image " << image_name;
        exit(0);
    }

    SDL_Texture *texture = IMG_LoadTexture(renderer, image_path.c_str());
    if (texture == nullptr) {
        std::cout << "error: missing image " << image_name;
        exit(0);
    }

    image_cache[image_name] = texture;
    return texture;
}

// resource loading and draw queue entry points
void Renderer::QueueSceneImageDraw(const ImageDrawRequest &request) {
    if (ShouldDiscardSceneRequest(request)) return;
    scene_draw_requests.push_back(request);
}

// resource loading and draw queue entry points
void Renderer::QueueSceneParticleBatch(const ParticleBatchRequest &request) {
    if (request.emitter_id < 0) return;
    scene_particle_batches.push_back(request);
}

// resource loading and draw queue entry points
void Renderer::QueueUIImageDraw(const ImageDrawRequest &request) {
    if (ShouldDiscardUIRequest(request)) return;
    ui_draw_requests.push_back(request);
}

// resource loading and draw queue entry points
void Renderer::QueuePixelDraw(const PixelDrawRequest &request) {
    pixel_draw_requests.push_back(request);
}

// text draw helpers
void Renderer::DrawText(const std::string &text_content,
                        const std::string &font_name, int font_size,
                        SDL_Color font_color, int x, int y) {
    if (font_name.empty()) return;
    text_draw_requests.push_back( {text_content, font_name, font_size, font_color, x, y});
}

// Render one frame by flushing queued scene images, particles, text, and pixels.
void Renderer::RenderFrame(SDL_Renderer *renderer, float camera_x,
                           float camera_y, float zoom_factor,
                           int camera_width, int camera_height) {
    RenderAndClearAllImages(renderer, camera_x, camera_y, zoom_factor,
                            camera_width, camera_height);
    RenderAndClearAllText(renderer);
    RenderAndClearAllPixels(renderer);
    PruneTextCache();
}

// Flush queued world-space image and particle draw requests for this frame.
void Renderer::RenderAndClearAllImages(SDL_Renderer *renderer, float camera_x,
                                       float camera_y, float zoom_factor,
                                       int camera_width, int camera_height) {
    if (renderer == nullptr) {
        scene_draw_requests.clear();
        scene_particle_batches.clear();
        ui_draw_requests.clear();
        return;
    }

    scene_draw_requests.erase(
        std::remove_if(scene_draw_requests.begin(), scene_draw_requests.end(),
                       ShouldDiscardSceneRequest),
        scene_draw_requests.end());
    ui_draw_requests.erase(
        std::remove_if(ui_draw_requests.begin(), ui_draw_requests.end(),
                       ShouldDiscardUIRequest),
        ui_draw_requests.end());

    if (scene_draw_requests.size() > 1) {
        std::stable_sort(scene_draw_requests.begin(), scene_draw_requests.end(),
                         CompareImageRequests);
    }
    if (scene_particle_batches.size() > 1) {
        std::stable_sort(scene_particle_batches.begin(),
                         scene_particle_batches.end(),
                         CompareParticleBatchRequests);
    }
    if (ui_draw_requests.size() > 1) {
        std::stable_sort(ui_draw_requests.begin(), ui_draw_requests.end(),
                         CompareImageRequests);
    }

    const float safe_zoom = (zoom_factor > 0.0f) ? zoom_factor : 1.0f;
    const float viewport_width = static_cast<float>(camera_width) * (1.0f / safe_zoom);
    const float viewport_height = static_cast<float>(camera_height) * (1.0f / safe_zoom);

    const auto render_scene_request = [&](const ImageDrawRequest &request) {
        SDL_Texture *texture = LoadTexture(request.image_name, renderer);
        if (texture == nullptr) return;

        float texture_w = 0.0f;
        float texture_h = 0.0f;
        SDLRenderHelper::SDL_QueryTexture(texture, &texture_w, &texture_h);

        SDL_RendererFlip flip = SDL_FLIP_NONE;
        if (request.scale_x < 0.0f) {
            flip = static_cast<SDL_RendererFlip>(flip | SDL_FLIP_HORIZONTAL);
        }
        if (request.scale_y < 0.0f) {
            flip = static_cast<SDL_RendererFlip>(flip | SDL_FLIP_VERTICAL);
        }

        const float x_scale = glm::abs(request.scale_x);
        const float y_scale = glm::abs(request.scale_y);

        SDL_FRect dst;
        dst.w = texture_w * x_scale;
        dst.h = texture_h * y_scale;

        SDL_FPoint pivot = {request.pivot_x * dst.w, request.pivot_y * dst.h};

        const float final_x = request.x - camera_x;
        const float final_y = request.y - camera_y;
        dst.x = final_x * kPixelsPerUnit +
                static_cast<float>(camera_width) * 0.5f * (1.0f / safe_zoom) -
                pivot.x;
        dst.y = final_y * kPixelsPerUnit +
                static_cast<float>(camera_height) * 0.5f * (1.0f / safe_zoom) -
                pivot.y;

        if (IsPivotBoundOutsideViewport(dst, pivot, viewport_width,
                                        viewport_height)) {
            return;
        }

        SDL_SetTextureColorMod(texture, static_cast<Uint8>(ClampByte(request.r)),
                               static_cast<Uint8>(ClampByte(request.g)),
                               static_cast<Uint8>(ClampByte(request.b)));
        SDL_SetTextureAlphaMod(texture, static_cast<Uint8>(ClampByte(request.a)));

        SDLRenderHelper::SDL_RenderCopyEx(
            Actor::kInvalidUID, "", renderer, texture, nullptr, &dst,
            request.rotation_degrees,
            &pivot, flip);

        SDL_SetTextureColorMod(texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture, 255);
    };

    SDL_RenderSetScale(renderer, safe_zoom, safe_zoom);
    size_t scene_request_index = 0;
    size_t particle_batch_index = 0;
    while (scene_request_index < scene_draw_requests.size() ||
           particle_batch_index < scene_particle_batches.size()) {
        const bool should_render_scene_request =
            (particle_batch_index >= scene_particle_batches.size()) ||
            (scene_request_index < scene_draw_requests.size() &&
             scene_draw_requests[scene_request_index].sorting_order <=
                 scene_particle_batches[particle_batch_index].sorting_order);

        if (should_render_scene_request) {
            render_scene_request(scene_draw_requests[scene_request_index]);
            ++scene_request_index;
            continue;
        }

        ParticleManager::RenderEmitterBatch(
            scene_particle_batches[particle_batch_index].emitter_id, renderer,
            camera_x, camera_y, zoom_factor, camera_width, camera_height);
        ++particle_batch_index;
    }
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);

    for (const ImageDrawRequest &request : ui_draw_requests) {
        SDL_Texture *texture = LoadTexture(request.image_name, renderer);
        if (texture == nullptr) continue;

        float texture_w = 0.0f;
        float texture_h = 0.0f;
        SDLRenderHelper::SDL_QueryTexture(texture, &texture_w, &texture_h);

        SDL_FRect dst = {request.x, request.y, texture_w, texture_h};
        SDL_SetTextureColorMod(texture, static_cast<Uint8>(ClampByte(request.r)),
                               static_cast<Uint8>(ClampByte(request.g)),
                               static_cast<Uint8>(ClampByte(request.b)));
        SDL_SetTextureAlphaMod(texture, static_cast<Uint8>(ClampByte(request.a)));

        SDLRenderHelper::SDL_RenderCopyEx(Actor::kInvalidUID, "", renderer,
                                          texture, nullptr,
                                          &dst, 0.0f, nullptr, SDL_FLIP_NONE);

        SDL_SetTextureColorMod(texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture, 255);
    }

    scene_draw_requests.clear();
    scene_particle_batches.clear();
    ui_draw_requests.clear();
}

// Flush queued text draw requests and reuse cached text textures when possible.
void Renderer::RenderAndClearAllText(SDL_Renderer *renderer) {
    for (const TextDrawRequest &req : text_draw_requests) {
        std::ostringstream key_stream;
        key_stream << req.font_name << '|'
                   << req.font_size << '|'
                   << static_cast<int>(req.color.r) << ','
                   << static_cast<int>(req.color.g) << ','
                   << static_cast<int>(req.color.b) << ','
                   << static_cast<int>(req.color.a) << '|'
                   << req.text;
        const std::string cache_key = key_stream.str();

        auto cache_it = text_cache.find(cache_key);
        if (cache_it == text_cache.end()) {
            TTF_Font *font = GetFont(req.font_name, req.font_size);
            if (font == nullptr) continue;

            SDL_Surface *surface = TTF_RenderText_Solid(font, req.text.c_str(), req.color);
            if (surface == nullptr) continue;

            SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
            if (texture != nullptr) {
                CachedTextTexture cached;
                cached.texture = texture;
                cached.width = surface->w;
                cached.height = surface->h;
                cached.last_used_timestamp = SDL_GetTicks();
                cache_it = text_cache.emplace(cache_key, cached).first;
            }
            SDL_FreeSurface(surface);
        }

        if (cache_it == text_cache.end()) continue;
        if (cache_it->second.texture == nullptr) continue;
        cache_it->second.last_used_timestamp = SDL_GetTicks();

        SDL_FRect dest = {static_cast<float>(req.x), static_cast<float>(req.y),
                          static_cast<float>(cache_it->second.width),
                          static_cast<float>(cache_it->second.height)};
        SDLRenderHelper::SDL_RenderCopyEx(Actor::kInvalidUID, "", renderer,
                                          cache_it->second.texture, nullptr,
                                          &dest, 0.0f, nullptr, SDL_FLIP_NONE);
    }

    text_draw_requests.clear();
}

// Flush queued pixel draw requests for debug/overlay rendering.
void Renderer::RenderAndClearAllPixels(SDL_Renderer *renderer) {
    if (renderer == nullptr) {
        pixel_draw_requests.clear();
        return;
    }
    if (pixel_draw_requests.empty()) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (const PixelDrawRequest &request : pixel_draw_requests) {
        SDL_SetRenderDrawColor(renderer, ClampByte(request.r), ClampByte(request.g),
                               ClampByte(request.b), ClampByte(request.a));
        SDL_RenderDrawPoint(renderer, request.x, request.y);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    pixel_draw_requests.clear();
}

// Clear resource caches and per-frame draw queues.
void Renderer::ClearCache() {
    for (auto &entry : image_cache) {
        if (entry.second != nullptr) SDL_DestroyTexture(entry.second);
    }
    image_cache.clear();
    scene_draw_requests.clear();
    scene_particle_batches.clear();
    ui_draw_requests.clear();
    pixel_draw_requests.clear();
}

void Engine::render() {
    // Rendering only fills the backbuffer. Presenting is owned by the outer
    // loop so runtime-only and editor-driven execution share one path.
    // 现在: render 只填充 backbuffer, PresentFrame 负责交换前后缓冲. 
    // 这样 runtime-only 和 editor-driven 执行就共用一套渲染流程了.
    if (render_runtime_to_texture_) {
        ensureRuntimeRenderTarget();
        SDL_SetRenderTarget(renderer, runtime_render_target_);
    } else {
        SDL_SetRenderTarget(renderer, nullptr);
    }
    SDL_RenderSetViewport(renderer, nullptr);
    clearFrame();

    // Built-in render components contribute draw requests during the render
    // phase so frozen editor frames still show authored scene content.
    ComponentManager::ResolveTransformHierarchy();
    ComponentManager::QueueBuiltinRenderers();
    ParticleManager::QueueRenderBatches();

    const float zoom_factor = std::clamp(runtime_zoom_factor, kMinZoomFactor, kMaxZoomFactor);
    Renderer::RenderFrame(renderer, camera_position.x, camera_position.y,
                          zoom_factor, config_.window_width,
                          config_.window_height);
    RecordRuntimeRenderFrame();

    if (render_runtime_to_texture_) {
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_RenderSetViewport(renderer, nullptr);
        clearEditorHostFrame();
    }
}

void Engine::clearFrame() {
    SDL_SetRenderDrawColor(renderer, config_.clear_color_r, config_.clear_color_g,
                           config_.clear_color_b, 255);
    SDL_RenderClear(renderer);
}

void Engine::clearEditorHostFrame() {
    // The editor chrome owns the host window. Once the runtime has been drawn
    // into its offscreen texture, reset the backbuffer to a neutral color so
    // Dear ImGui can compose the dockspace and panels on top.
    SDL_SetRenderDrawColor(renderer, 18, 22, 30, 255);
    SDL_RenderClear(renderer);
}
