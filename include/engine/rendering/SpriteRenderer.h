#ifndef SPRITE_RENDERER_COMPONENT_H
#define SPRITE_RENDERER_COMPONENT_H

#include <string>

struct Actor;

class SpriteRenderer {
public:
    SpriteRenderer() = default;
    SpriteRenderer(const SpriteRenderer &) = default;

    Actor *actor = nullptr;
    std::string key = "";
    bool enabled = true;

    std::string sprite = "???";
    int r = 255;
    int g = 255;
    int b = 255;
    int a = 255;
    float pivot_x = 0.5f;
    float pivot_y = 0.5f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    int sorting_order = 0;
    bool auto_sorting_order = false;

    // Queue one world-space draw using either Transform or Rigidbody data from
    // the owning actor. This runs during the render phase, so editor frozen
    // frames still show sprites.
    void QueueDraw() const;
};

#endif
