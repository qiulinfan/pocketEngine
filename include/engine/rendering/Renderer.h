#ifndef RENDERER_H
#define RENDERER_H

#include "SDL2/SDL.h"
#include "SDL2_ttf/SDL_ttf.h"
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

struct ImageDrawRequest {
    // basic image name and world-space transform
    // 基本图片名和世界坐标变换
    std::string image_name;
    float x = 0.0f;
    float y = 0.0f;
    float rotation_degrees = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;

    // normalized pivot and RGBA modulation
    // 归一化 pivot 和 RGBA 调制
    float pivot_x = 0.5f;
    float pivot_y = 0.5f;
    int r = 255;
    int g = 255;
    int b = 255;
    int a = 255;
    int sorting_order = 0;
};

struct PixelDrawRequest {
    int x = 0;
    int y = 0;
    int r = 255;
    int g = 255;
    int b = 255;
    int a = 255;
};

struct TextDrawRequest {
    // text content, font choice, and screen position
    // 文本内容, 字体选择和屏幕坐标
    std::string text;
    std::string font_name;
    int font_size = 16;
    SDL_Color color = {255, 255, 255, 255};
    int x = 0;
    int y = 0;
};

struct CachedTextTexture {
    SDL_Texture *texture = nullptr;
    int width = 0;
    int height = 0;
    Uint32 last_used_timestamp = 0;
};

struct ParticleBatchRequest {
    int emitter_id = -1;
    int sorting_order = 0;
};

class Renderer {
public:
    // used when particles do not specify an image
    // 粒子没有指定图片时使用
    static inline constexpr const char *kDefaultParticleTextureName = "__default_particle__";

    static void Init();
    static void Shutdown();

    // resource loading and draw queue entry points
    // 资源加载和绘制队列入口
    static SDL_Texture *LoadTexture(const std::string &image_name,
                                    SDL_Renderer *renderer);
    static void QueueSceneImageDraw(const ImageDrawRequest &request);
    static void QueueSceneParticleBatch(const ParticleBatchRequest &request);
    static void QueueUIImageDraw(const ImageDrawRequest &request);
    static void QueuePixelDraw(const PixelDrawRequest &request);

    // text draw helpers
    // 文本绘制辅助接口
    static void DrawText(const std::string &text_content,
                         const std::string &font_name, int font_size,
                         SDL_Color font_color, int x, int y);

    static void RenderFrame(SDL_Renderer *renderer, float camera_x,
                            float camera_y, float zoom_factor,
                            int camera_width, int camera_height);
    static void ClearCache();

private:
    static TTF_Font *GetFont(const std::string &font_name, int size);
    static void PruneTextCache();
    static void RenderAndClearAllImages(SDL_Renderer *renderer, float camera_x,
                                        float camera_y, float zoom_factor,
                                        int camera_width, int camera_height);
    static void RenderAndClearAllText(SDL_Renderer *renderer);
    static void RenderAndClearAllPixels(SDL_Renderer *renderer);

    // caches and per-frame draw queues
    // 资源缓存和逐帧绘制队列
    static inline bool initialized = false;
    static inline std::unordered_map<std::string, SDL_Texture *> image_cache;
    static inline std::unordered_map<std::string, std::unordered_map<int, TTF_Font *>>
        font_cache;
    static inline std::unordered_map<std::string, CachedTextTexture> text_cache;
    static inline std::vector<ImageDrawRequest> scene_draw_requests;
    static inline std::vector<ParticleBatchRequest> scene_particle_batches;
    static inline std::vector<ImageDrawRequest> ui_draw_requests;
    static inline std::vector<PixelDrawRequest> pixel_draw_requests;
    static inline std::vector<TextDrawRequest> text_draw_requests;
};

#endif
