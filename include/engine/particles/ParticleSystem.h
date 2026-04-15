#ifndef PARTICLESYSTEM_H
#define PARTICLESYSTEM_H

#include "particles/RandomEngine.h"
#include <limits>
#include <string>

struct Actor;

class ParticleSystem {
public:
    ParticleSystem() = default;
    ParticleSystem(const ParticleSystem &) = default;

    Actor *actor = nullptr;
    std::string key = "";
    bool enabled = true;

    // emitter origin and auto-burst settings
    // 发射器原点和自动 burst 设置
    float x = 0.0f;
    float y = 0.0f;
    int frames_between_bursts = 1;
    int burst_quantity = 1;
    int duration_frames = 300;

    // randomized initial scale, speed, rotation
    // 随机初始缩放, 速度和旋转
    float start_scale_min = 1.0f;
    float start_scale_max = 1.0f;
    float start_speed_min = 0.0f;
    float start_speed_max = 0.0f;
    float rotation_min = 0.0f;
    float rotation_max = 0.0f;
    float rotation_speed_min = 0.0f;
    float rotation_speed_max = 0.0f;

    // start and optional end colors
    // 起始颜色和可选结束颜色
    int start_color_r = 255;
    int start_color_g = 255;
    int start_color_b = 255;
    int start_color_a = 255;
    int end_color_r = -1;
    int end_color_g = -1;
    int end_color_b = -1;
    int end_color_a = -1;

    // render and emission shape settings
    // 渲染设置和发射形状设置
    std::string image = "";
    int sorting_order = 9999;
    float emit_angle_min = 0.0f;
    float emit_angle_max = 360.0f;
    float emit_radius_min = 0.0f;
    float emit_radius_max = 0.5f;
    float gravity_scale_x = 0.0f;
    float gravity_scale_y = 0.0f;
    float drag_factor = 1.0f;
    float angular_drag_factor = 1.0f;
    float end_scale = std::numeric_limits<float>::quiet_NaN();

    void OnStart();
    void OnUpdate();
    void OnDestroy();

    // stop / resume automatic emission
    // 停止 / 恢复自动发射
    void Stop();
    void Play();

    // emit one burst immediately, ignoring normal cadence
    // 立即发出一波粒子, 不受正常节奏限制
    void Burst();

    // sync current enabled state into ParticleManager
    // 将当前 enabled 状态同步到 ParticleManager
    void SyncRuntimeState();

private:
    // lazy setup of distributions and backing emitter
    // 延迟初始化随机分布器和底层 emitter
    void InitializeIfNeeded();
    void EmitBurst(int particle_count);
    static int ClampMinimumOne(int value);
    static int ClampByte(int value);

    RandomEngine emit_angle_distribution;
    RandomEngine emit_radius_distribution;
    RandomEngine rotation_distribution;
    RandomEngine scale_distribution;
    RandomEngine speed_distribution;
    RandomEngine rotation_speed_distribution;

    // runtime emitter state owned by ParticleManager
    // ParticleManager 持有的运行时 emitter 状态
    int emitter_id = -1;
    int local_frame_number = 0;
    bool emission_allowed = true;
    bool initialized = false;
};

#endif
