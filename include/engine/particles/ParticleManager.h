#ifndef PARTICLEMANAGER_H
#define PARTICLEMANAGER_H

#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Renderer;

struct ParticleEmitterConfig;

struct ParticleEmitterConfig {
    // particle lifetime and reserve hint
    // 粒子生命周期和预留容量提示
    int duration_frames = 300;
    size_t reserve_hint = 0;

    // per-frame gravity and drag
    // 每帧重力和阻尼
    float gravity_scale_x = 0.0f;
    float gravity_scale_y = 0.0f;
    float drag_factor = 1.0f;
    float angular_drag_factor = 1.0f;

    // optional end-of-life scale and color interpolation
    // 可选的结束缩放和结束颜色插值
    bool has_end_scale = false;
    float end_scale = 1.0f;
    bool has_end_color_r = false;
    bool has_end_color_g = false;
    bool has_end_color_b = false;
    bool has_end_color_a = false;
    int end_color_r = 255;
    int end_color_g = 255;
    int end_color_b = 255;
    int end_color_a = 255;
};

struct ParticleSpawnRequest {
    // initial transform and velocity
    // 初始位置, 旋转和速度
    float x = 0.0f;
    float y = 0.0f;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    float scale = 1.0f;
    float rotation = 0.0f;
    float angular_velocity = 0.0f;

    // initial color and render info
    // 初始颜色和渲染信息
    int color_r = 255;
    int color_g = 255;
    int color_b = 255;
    int color_a = 255;
    int image_id = -1;
    std::string image_name = "";
    int sorting_order = 9999;
};

class ParticleManager {
public:
    // emitter lifecycle
    // emitter 生命周期
    static int CreateEmitter();
    static void DestroyEmitter(int emitter_id);
    static bool IsEmitterAlive(int emitter_id);
    static void ConfigureEmitter(int emitter_id,
                                 const ParticleEmitterConfig &config);
    static void SetEmitterRuntimeEnabled(int emitter_id, bool runtime_enabled);
    static int ResolveImageId(const std::string &image_name);

    // spawn, simulate, queue, render
    // 生成, 更新, 排队, 渲染
    static void SpawnParticle(int emitter_id, const ParticleSpawnRequest &request);
    static void Update(float dt);
    static void QueueRenderBatches();
    static void RenderEmitterBatch(int emitter_id, SDL_Renderer *renderer,
                                   float camera_x, float camera_y,
                                   float zoom_factor, int camera_width,
                                   int camera_height);
    static void Clear();

private:
    struct EmitterSlot {
        // emitter state and config
        // emitter 状态和配置
        bool active = false;
        bool runtime_enabled = true;
        ParticleEmitterConfig config;
        int frame_number = 0;

        // particle SoA storage for transform and motion
        // 粒子的 SoA 形式位置, 速度, 缩放, 旋转存储
        std::vector<float> pos_x;
        std::vector<float> pos_y;
        std::vector<float> velocity_x;
        std::vector<float> velocity_y;
        std::vector<float> scale;
        std::vector<float> initial_scale;
        std::vector<float> rotation;
        std::vector<float> angular_velocity;

        // current and initial color caches
        // 当前颜色和初始颜色缓存
        std::vector<int> color_r;
        std::vector<int> color_g;
        std::vector<int> color_b;
        std::vector<int> color_a;
        std::vector<int> initial_color_r;
        std::vector<int> initial_color_g;
        std::vector<int> initial_color_b;
        std::vector<int> initial_color_a;

        // image / sorting metadata, plus a fast path for uniform render state
        // 图像和排序元数据, 以及统一渲染态的快速路径
        std::vector<int> image_ids;
        std::vector<int> sorting_orders;
        int shared_image_id = 0;
        int shared_sorting_order = 9999;
        bool has_uniform_render_state = true;

        // per-particle lifetime bookkeeping and free-list reuse
        // 粒子生命周期记录和空闲索引复用
        std::vector<int> start_frame;
        std::vector<int> is_active;
        std::deque<int> free_list;
        size_t active_count = 0;
        size_t reserved_capacity = 0;

        void ClearParticles();
        void EnsureParticleCapacity(size_t capacity);
    };

    // intern image names into integer ids for cheaper storage and comparison
    // 把图片名 intern 成整数 id, 降低存储和比较成本
    static int InternImageName(const std::string &image_name);
    static int ClampByte(int value);

    static inline std::vector<EmitterSlot> emitters;
    static inline std::vector<int> free_emitters;
    static inline std::unordered_map<std::string, int> image_name_to_id;
    static inline std::vector<std::string> image_names;
};

#endif
