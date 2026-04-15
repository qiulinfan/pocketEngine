#ifndef ENGINE_PARTICLES_RANDOM_ENGINE_H
#define ENGINE_PARTICLES_RANDOM_ENGINE_H

#include <random>

// Lightweight seeded random sampler used by particle distributions.
// 轻量级带种子随机采样器, 供粒子分布使用.
class RandomEngine {
public:
    RandomEngine() = default;

    RandomEngine(float min, float max, int seed) {
        Configure(min, max, seed);
    }

    void Configure(float min, float max, int seed) {
        engine_ = std::default_random_engine(seed);
        distribution_ = std::uniform_real_distribution<float>(min, max);
    }

    float Sample() {
        return distribution_(engine_);
    }

private:
    std::default_random_engine engine_;
    std::uniform_real_distribution<float> distribution_;
};

#endif
