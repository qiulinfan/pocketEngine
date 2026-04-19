#include "particles/ParticleManager.h"
#include "rendering/Renderer.h"
#include "rendering/SDLRenderHelper.h"
#include <algorithm>
#include <cmath>
#include "glm/glm.hpp"

namespace {

constexpr float kPixelsPerUnit = 100.0f;

// 线性插值: 用于在粒子生命周期内平滑过渡属性值
float Lerp(float start, float end, float t) {
    return start + (end - start) * t;
}

// 将结果 clamped 到 [0, 255] 范围内, 用于颜色通道的插值
int LerpByte(int start, int end, float t) {
    const float interpolated = Lerp(static_cast<float>(start),
                                    static_cast<float>(end), t);
    return std::clamp(static_cast<int>(interpolated), 0, 255);
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

} // namespace

// Reserve space for one emitter's SoA buffers before particles are spawned.
void ParticleManager::EmitterSlot::EnsureParticleCapacity(size_t capacity) {
    if (capacity <= reserved_capacity) return;

    pos_x.reserve(capacity);
    pos_y.reserve(capacity);
    velocity_x.reserve(capacity);
    velocity_y.reserve(capacity);
    scale.reserve(capacity);
    initial_scale.reserve(capacity);
    rotation.reserve(capacity);
    angular_velocity.reserve(capacity);

    color_r.reserve(capacity);
    color_g.reserve(capacity);
    color_b.reserve(capacity);
    color_a.reserve(capacity);
    initial_color_r.reserve(capacity);
    initial_color_g.reserve(capacity);
    initial_color_b.reserve(capacity);
    initial_color_a.reserve(capacity);

    image_ids.reserve(capacity);
    sorting_orders.reserve(capacity);
    start_frame.reserve(capacity);
    is_active.reserve(capacity);

    reserved_capacity = capacity;
}

// emitter lifecycle
// Effect: Create a new emitter and return its ID. Reuse free emitter slots if available.
// 创建一个新的 emitter 并返回它的 ID. 如果有可用的空闲 emitter槽位, 则 reuse.
int ParticleManager::CreateEmitter() {
    if (!free_emitters.empty()) {
        const int emitter_id = free_emitters.back();
        free_emitters.pop_back();
        emitters[emitter_id].active = true;
        emitters[emitter_id].runtime_enabled = true;
        emitters[emitter_id].ClearParticles();
        return emitter_id;
    }

    emitters.emplace_back();
    emitters.back().active = true;
    emitters.back().runtime_enabled = true;
    return static_cast<int>(emitters.size()) - 1;
}


// Reset one emitter slot to an empty runtime state while keeping its storage.
// Effect: Clear all paticles in an emitter
// 清除一个 emitter 中的所有粒子
void ParticleManager::EmitterSlot::ClearParticles() {
    pos_x.clear();
    pos_y.clear();
    velocity_x.clear();
    velocity_y.clear();
    scale.clear();
    initial_scale.clear();
    rotation.clear();
    angular_velocity.clear();
    color_r.clear();
    color_g.clear();
    color_b.clear();
    color_a.clear();
    initial_color_r.clear();
    initial_color_g.clear();
    initial_color_b.clear();
    initial_color_a.clear();
    image_ids.clear();
    sorting_orders.clear();
    shared_image_id = 0;
    shared_sorting_order = 9999;
    has_uniform_render_state = true;
    start_frame.clear();
    is_active.clear();
    free_list.clear();
    active_count = 0;
    frame_number = 0;
    runtime_enabled = true;
    config = ParticleEmitterConfig();
}

// emitter lifecycle
// Effect: Destroy an emitter and free its slot
// 销毁一个 emitter 并释放它的槽位
void ParticleManager::DestroyEmitter(int emitter_id) {
    if (!IsEmitterAlive(emitter_id)) return;

    emitters[emitter_id].active = false;
    emitters[emitter_id].ClearParticles();
    free_emitters.push_back(emitter_id);
}


// emitter lifecycle
// Effect: Check if an emitter is alive
// 检查一个 emitter 是否存活
bool ParticleManager::IsEmitterAlive(int emitter_id) {
    if (emitter_id < 0) return false;
    if (static_cast<size_t>(emitter_id) >= emitters.size()) return false;
    return emitters[emitter_id].active;
}

// emitter lifecycle
void ParticleManager::ConfigureEmitter(int emitter_id,
                                       const ParticleEmitterConfig &config) {
    if (!IsEmitterAlive(emitter_id)) return;
    emitters[emitter_id].config = config;
    if (emitters[emitter_id].config.duration_frames < 1) {
        emitters[emitter_id].config.duration_frames = 1;
    }
    emitters[emitter_id].EnsureParticleCapacity(emitters[emitter_id].config.reserve_hint);
}

// emitter lifecycle
void ParticleManager::SetEmitterRuntimeEnabled(int emitter_id,
                                               bool runtime_enabled) {
    if (!IsEmitterAlive(emitter_id)) return;
    emitters[emitter_id].runtime_enabled = runtime_enabled;
}

// emitter lifecycle
int ParticleManager::ResolveImageId(const std::string &image_name) {
    return InternImageName(image_name);
}

// spawn, simulate, queue, render
// Effect: Spawn a particle in an emitter based on the given request. If the emitter is not alive, do nothing.
// 根据给定的请求在一个 emitter 中生成一个粒子. 如果 emitter 不存
void ParticleManager::SpawnParticle(int emitter_id,
                                    const ParticleSpawnRequest &request) {
    if (!IsEmitterAlive(emitter_id)) return;

    EmitterSlot &emitter = emitters[emitter_id];
    int image_id = request.image_id;
    if (image_id < 0 || static_cast<size_t>(image_id) >= image_names.size()) {
        image_id = InternImageName(request.image_name);
    }
    if (emitter.active_count == 0) {
        emitter.shared_image_id = image_id;
        emitter.shared_sorting_order = request.sorting_order;
        emitter.has_uniform_render_state = true;
    } else if (emitter.has_uniform_render_state &&
               (emitter.shared_image_id != image_id ||
                emitter.shared_sorting_order != request.sorting_order)) {
        emitter.has_uniform_render_state = false;
    }

    size_t index = 0;
    if (!emitter.free_list.empty()) {
        index = static_cast<size_t>(emitter.free_list.front());
        emitter.free_list.pop_front();
    } else {
        index = emitter.pos_x.size();
        emitter.pos_x.push_back(0.0f);
        emitter.pos_y.push_back(0.0f);
        emitter.velocity_x.push_back(0.0f);
        emitter.velocity_y.push_back(0.0f);
        emitter.scale.push_back(1.0f);
        emitter.initial_scale.push_back(1.0f);
        emitter.rotation.push_back(0.0f);
        emitter.angular_velocity.push_back(0.0f);
        emitter.color_r.push_back(255);
        emitter.color_g.push_back(255);
        emitter.color_b.push_back(255);
        emitter.color_a.push_back(255);
        emitter.initial_color_r.push_back(255);
        emitter.initial_color_g.push_back(255);
        emitter.initial_color_b.push_back(255);
        emitter.initial_color_a.push_back(255);
        emitter.image_ids.push_back(0);
        emitter.sorting_orders.push_back(0);
        emitter.start_frame.push_back(0);
        emitter.is_active.push_back(false);
    }

    emitter.pos_x[index] = request.x;
    emitter.pos_y[index] = request.y;
    emitter.velocity_x[index] = request.velocity_x;
    emitter.velocity_y[index] = request.velocity_y;
    emitter.scale[index] = request.scale;
    emitter.initial_scale[index] = request.scale;
    emitter.rotation[index] = request.rotation;
    emitter.angular_velocity[index] = request.angular_velocity;
    emitter.color_r[index] = ClampByte(request.color_r);
    emitter.color_g[index] = ClampByte(request.color_g);
    emitter.color_b[index] = ClampByte(request.color_b);
    emitter.color_a[index] = ClampByte(request.color_a);
    emitter.initial_color_r[index] = emitter.color_r[index];
    emitter.initial_color_g[index] = emitter.color_g[index];
    emitter.initial_color_b[index] = emitter.color_b[index];
    emitter.initial_color_a[index] = emitter.color_a[index];
    emitter.image_ids[index] = image_id;
    emitter.sorting_orders[index] = request.sorting_order;
    emitter.start_frame[index] = emitter.frame_number;
    emitter.is_active[index] = true;
    ++emitter.active_count;
}

// spawn, simulate, queue, render
void ParticleManager::Update(float dt) {
    (void)dt;
    for (EmitterSlot &emitter : emitters) {
        if (!emitter.active) continue;
        if (!emitter.runtime_enabled) continue;

        for (size_t i = 0; i < emitter.pos_x.size(); ++i) {
            if (!emitter.is_active[i]) continue;
            // Check if the particle's lifetime has expired: 将其标记为不活跃并放入 free list.
            const int frames_particle_has_been_alive = emitter.frame_number - emitter.start_frame[i];
            if (frames_particle_has_been_alive >= emitter.config.duration_frames) {
                emitter.is_active[i] = false;
                emitter.free_list.push_back(static_cast<int>(i));
                if (emitter.active_count > 0) {
                    --emitter.active_count;
                }
                continue;
            }


            // 更新活跃粒子的速度, 位置, 旋转
            emitter.velocity_x[i] += emitter.config.gravity_scale_x;
            emitter.velocity_y[i] += emitter.config.gravity_scale_y;

            emitter.velocity_x[i] *= emitter.config.drag_factor;
            emitter.velocity_y[i] *= emitter.config.drag_factor;
            emitter.angular_velocity[i] *= emitter.config.angular_drag_factor;

            emitter.pos_x[i] += emitter.velocity_x[i];
            emitter.pos_y[i] += emitter.velocity_y[i];
            emitter.rotation[i] += emitter.angular_velocity[i];

            const float lifetime_progress =
                static_cast<float>(frames_particle_has_been_alive) /
                static_cast<float>(emitter.config.duration_frames);
            if (emitter.config.has_end_scale) {
                emitter.scale[i] = Lerp(emitter.initial_scale[i],
                                        emitter.config.end_scale,
                                        lifetime_progress);
            }
            if (emitter.config.has_end_color_r) {
                emitter.color_r[i] = LerpByte(emitter.initial_color_r[i],
                                              emitter.config.end_color_r,
                                              lifetime_progress);
            }
            if (emitter.config.has_end_color_g) {
                emitter.color_g[i] = LerpByte(emitter.initial_color_g[i],
                                              emitter.config.end_color_g,
                                              lifetime_progress);
            }
            if (emitter.config.has_end_color_b) {
                emitter.color_b[i] = LerpByte(emitter.initial_color_b[i],
                                              emitter.config.end_color_b,
                                              lifetime_progress);
            }
            if (emitter.config.has_end_color_a) {
                emitter.color_a[i] = LerpByte(emitter.initial_color_a[i],
                                              emitter.config.end_color_a,
                                              lifetime_progress);
            }
        }

        ++emitter.frame_number;
    }
}

// spawn, simulate, queue, render
// Effect: Queue one particle batch per emitter when particles share render state.
// Mixed render state falls back to the old per-particle request path.
void ParticleManager::QueueRenderBatches() {
    for (size_t emitter_id = 0; emitter_id < emitters.size(); ++emitter_id) {
        const EmitterSlot &emitter = emitters[emitter_id];
        if (!emitter.active) continue;
        if (!emitter.runtime_enabled) continue;
        if (emitter.active_count == 0) continue;

        if (emitter.has_uniform_render_state) {
            ParticleBatchRequest request;
            request.emitter_id = static_cast<int>(emitter_id);
            request.sorting_order = emitter.shared_sorting_order;
            Renderer::QueueSceneParticleBatch(request);
            continue;
        }

        for (size_t i = 0; i < emitter.pos_x.size(); ++i) {
            if (!emitter.is_active[i]) continue;

            ImageDrawRequest request;
            request.image_name = image_names[emitter.image_ids[i]];
            request.x = emitter.pos_x[i];
            request.y = emitter.pos_y[i];
            request.rotation_degrees = emitter.rotation[i];
            request.scale_x = emitter.scale[i];
            request.scale_y = emitter.scale[i];
            request.pivot_x = 0.5f;
            request.pivot_y = 0.5f;
            request.r = emitter.color_r[i];
            request.g = emitter.color_g[i];
            request.b = emitter.color_b[i];
            request.a = emitter.color_a[i];
            request.sorting_order = emitter.sorting_orders[i];
            Renderer::QueueSceneImageDraw(request);
        }
    }
}

// spawn, simulate, queue, render
void ParticleManager::RenderEmitterBatch(int emitter_id, SDL_Renderer *renderer,
                                         float camera_x, float camera_y,
                                         float zoom_factor, int camera_width,
                                         int camera_height) {
    if (renderer == nullptr) return;
    if (!IsEmitterAlive(emitter_id)) return;

    const EmitterSlot &emitter = emitters[static_cast<size_t>(emitter_id)];
    if (!emitter.runtime_enabled) return;
    if (emitter.active_count == 0) return;

    SDL_Texture *texture = Renderer::LoadTexture(image_names[emitter.shared_image_id], renderer);
    if (texture == nullptr) return;

    float texture_w = 0.0f;
    float texture_h = 0.0f;
    SDLRenderHelper::SDL_QueryTexture(texture, &texture_w, &texture_h);

    const float safe_zoom = (zoom_factor > 0.0f) ? zoom_factor : 1.0f;
    const float viewport_width = static_cast<float>(camera_width) * (1.0f / safe_zoom);
    const float viewport_height = static_cast<float>(camera_height) * (1.0f / safe_zoom);
    bool has_last_color_mod = false;
    bool has_last_alpha_mod = false;
    int last_r = 255;
    int last_g = 255;
    int last_b = 255;
    int last_a = 255;

    for (size_t i = 0; i < emitter.pos_x.size(); ++i) {
        if (!emitter.is_active[i]) continue;
        if (emitter.color_a[i] <= 0) continue;
        if (emitter.scale[i] == 0.0f) continue;

        SDL_RendererFlip flip = SDL_FLIP_NONE;
        if (emitter.scale[i] < 0.0f) {
            flip = static_cast<SDL_RendererFlip>(flip | SDL_FLIP_HORIZONTAL);
            flip = static_cast<SDL_RendererFlip>(flip | SDL_FLIP_VERTICAL);
        }

        const float particle_scale = glm::abs(emitter.scale[i]);
        SDL_FRect dst;
        dst.w = texture_w * particle_scale;
        dst.h = texture_h * particle_scale;

        SDL_FPoint pivot = {0.5f * dst.w, 0.5f * dst.h};
        const float final_x = emitter.pos_x[i] - camera_x;
        const float final_y = emitter.pos_y[i] - camera_y;
        dst.x = final_x * kPixelsPerUnit +
                static_cast<float>(camera_width) * 0.5f * (1.0f / safe_zoom) -
                pivot.x;
        dst.y = final_y * kPixelsPerUnit +
                static_cast<float>(camera_height) * 0.5f * (1.0f / safe_zoom) -
                pivot.y;

        if (IsPivotBoundOutsideViewport(dst, pivot, viewport_width,
                                        viewport_height)) {
            continue;
        }

        const int color_r = emitter.color_r[i];
        const int color_g = emitter.color_g[i];
        const int color_b = emitter.color_b[i];
        const int color_a = emitter.color_a[i];

        if (!has_last_color_mod || last_r != color_r || last_g != color_g ||
            last_b != color_b) {
            SDL_SetTextureColorMod(texture, static_cast<Uint8>(color_r),
                                   static_cast<Uint8>(color_g),
                                   static_cast<Uint8>(color_b));
            last_r = color_r;
            last_g = color_g;
            last_b = color_b;
            has_last_color_mod = true;
        }
        if (!has_last_alpha_mod || last_a != color_a) {
            SDL_SetTextureAlphaMod(texture, static_cast<Uint8>(color_a));
            last_a = color_a;
            has_last_alpha_mod = true;
        }

        SDLRenderHelper::SDL_RenderCopyEx(Actor::kInvalidUID, "", renderer,
                                          texture, nullptr,
                                          &dst, emitter.rotation[i], &pivot,
                                          flip);
    }
}

// spawn, simulate, queue, render
// Effect: Clear all emitters and related data. 
// Used when changing scenes or resetting the game.
// 清除所有 emitter 和相关数据. 这可以在切换场景或重置游戏时使用.
void ParticleManager::Clear() {
    emitters.clear();
    free_emitters.clear();
    image_name_to_id.clear();
    image_names.clear();
}

// intern image names into integer ids for cheaper storage and comparison
// Effect: Get or create an integer ID for an image name. 
// This is used to optimize memory usage and comparison when handling particles.
// 为图像名称获取或创建一个整数 ID. 这用于在处理粒子时优化内存使用和比较.
int ParticleManager::InternImageName(const std::string &image_name) {
    const std::string resolved_name = image_name.empty()
                                          ? std::string(Renderer::kDefaultParticleTextureName)
                                          : image_name;
    auto it = image_name_to_id.find(resolved_name);
    if (it != image_name_to_id.end()) {
        return it->second;
    }

    const int image_id = static_cast<int>(image_names.size());
    image_names.push_back(resolved_name);
    image_name_to_id.emplace(resolved_name, image_id);
    return image_id;
}

// Clamp color channels before they are stored into packed particle buffers.
int ParticleManager::ClampByte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}
