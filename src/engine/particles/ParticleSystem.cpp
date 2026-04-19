#include "particles/ParticleSystem.h"
#include "particles/ParticleManager.h"
#include "glm/glm.hpp"
#include <algorithm>
#include <cmath>

namespace {

bool HasEndScaleValue(float value) {
    return !std::isnan(value);
}

bool HasValidColorChannel(int value) {
    return value >= 0 && value <= 255;
}

ParticleEmitterConfig BuildEmitterConfig(const ParticleSystem &system) {
    ParticleEmitterConfig config;
    config.duration_frames = std::max(system.duration_frames, 1);
    const int burst_interval = std::max(system.frames_between_bursts, 1);
    const int burst_quantity = std::max(system.burst_quantity, 1);
    const int bursts_while_particles_live =
        (config.duration_frames + burst_interval - 1) / burst_interval;
    config.reserve_hint = static_cast<size_t>(bursts_while_particles_live) *
                          static_cast<size_t>(burst_quantity);
    config.gravity_scale_x = system.gravity_scale_x;
    config.gravity_scale_y = system.gravity_scale_y;
    config.drag_factor = system.drag_factor;
    config.angular_drag_factor = system.angular_drag_factor;
    config.has_end_scale = HasEndScaleValue(system.end_scale);
    config.end_scale = config.has_end_scale ? system.end_scale : 1.0f;
    config.has_end_color_r = HasValidColorChannel(system.end_color_r);
    config.has_end_color_g = HasValidColorChannel(system.end_color_g);
    config.has_end_color_b = HasValidColorChannel(system.end_color_b);
    config.has_end_color_a = HasValidColorChannel(system.end_color_a);
    config.end_color_r = config.has_end_color_r ? system.end_color_r : 255;
    config.end_color_g = config.has_end_color_g ? system.end_color_g : 255;
    config.end_color_b = config.has_end_color_b ? system.end_color_b : 255;
    config.end_color_a = config.has_end_color_a ? system.end_color_a : 255;
    return config;
}

} // namespace

// Initialize the emitter on first use and sync its enabled state into runtime.
void ParticleSystem::OnStart() {
    InitializeIfNeeded();
    SyncRuntimeState();
}

// Advance automatic emission cadence once per simulation frame.
void ParticleSystem::OnUpdate() {
    InitializeIfNeeded();
    const int burst_interval = ClampMinimumOne(frames_between_bursts);
    if (emission_allowed && local_frame_number % burst_interval == 0) {
        EmitBurst(ClampMinimumOne(burst_quantity));
    }
    ++local_frame_number;
}

// Release the backing emitter and reset transient runtime state.
void ParticleSystem::OnDestroy() {
    ParticleManager::DestroyEmitter(emitter_id);
    emitter_id = -1;
    local_frame_number = 0;
    emission_allowed = true;
    initialized = false;
}

// stop / resume automatic emission
void ParticleSystem::Stop() {
    emission_allowed = false;
}

// stop / resume automatic emission
void ParticleSystem::Play() {
    emission_allowed = true;
}

// Effect: 立即发出一波粒子, 无视 burst 间隔和当前帧数. 
// 可以在一些事件驱动的场景中使用, 比如玩家攻击时发出一波粒子.
void ParticleSystem::Burst() {
    InitializeIfNeeded();
    SyncRuntimeState();
    EmitBurst(ClampMinimumOne(burst_quantity));
}

// sync current enabled state into ParticleManager
// Effect: 将当前的 enabled 状态同步到 ParticleManager 中对应 emitter 的 runtime_enabled.
// 会影响 Update() 中粒子的更新和 QueueRenderBatches()/RenderEmitterBatch() 中的渲染,
// 但不会影响粒子的生成.
void ParticleSystem::SyncRuntimeState() {
    if (!initialized) return;
    ParticleManager::SetEmitterRuntimeEnabled(emitter_id, enabled);
}

// lazy setup of distributions and backing emitter
void ParticleSystem::InitializeIfNeeded() {
    if (initialized) return;

    // get min/max 并生成随机数分布器
    // 这一版: 完全不 handle 反常值(like min > max)
    emit_angle_distribution = RandomEngine(emit_angle_min, emit_angle_max, 298);
    emit_radius_distribution = RandomEngine(emit_radius_min, emit_radius_max, 404);
    rotation_distribution = RandomEngine(rotation_min, rotation_max, 440);
    speed_distribution = RandomEngine(start_speed_min, start_speed_max, 498);
    rotation_speed_distribution = RandomEngine(rotation_speed_min, rotation_speed_max, 305);
    scale_distribution = RandomEngine(start_scale_min, start_scale_max, 494);

    // 创建 emitter, claim emitter_id, 初始化 local_frame_number
    emitter_id = ParticleManager::CreateEmitter();
    ParticleManager::SetEmitterRuntimeEnabled(emitter_id, enabled);
    ParticleManager::ConfigureEmitter(emitter_id, BuildEmitterConfig(*this));
    local_frame_number = 0;
    initialized = true;
}

// Emit one burst of particles using the current randomized distribution state.
void ParticleSystem::EmitBurst(int particle_count) {
    ParticleSpawnRequest request;
    request.color_r = ClampByte(start_color_r);
    request.color_g = ClampByte(start_color_g);
    request.color_b = ClampByte(start_color_b);
    request.color_a = ClampByte(start_color_a);
    request.image_id = ParticleManager::ResolveImageId(image);
    request.sorting_order = sorting_order;

    for (int i = 0; i < particle_count; ++i) {
        const float angle_degrees = emit_angle_distribution.Sample();
        const float angle_radians = glm::radians(angle_degrees);
        const float radius = emit_radius_distribution.Sample();
        const float cos_angle = glm::cos(angle_radians);
        const float sin_angle = glm::sin(angle_radians);
        const float speed = speed_distribution.Sample();

        request.x = x + cos_angle * radius;
        request.y = y + sin_angle * radius;
        request.velocity_x = cos_angle * speed;
        request.velocity_y = sin_angle * speed;
        request.scale = scale_distribution.Sample();
        request.rotation = rotation_distribution.Sample();
        request.angular_velocity = rotation_speed_distribution.Sample();

        // spawn particle: 启用 ECS 接口, 交给 ParticleManager 处理 emitter 内粒子数据的存储和 render request 的生成
        ParticleManager::SpawnParticle(emitter_id, request);
    }
}

// Clamp burst-related integer fields so runtime never steps with zero cadence.
int ParticleSystem::ClampMinimumOne(int value) {
    return (value < 1) ? 1 : value;
}

// Clamp RGBA channel values into the byte range expected by the renderer.
int ParticleSystem::ClampByte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}
