#include "app/runtime/RuntimeApp.h"
#include "engine/core/Engine.h"
#include <iostream>

// Boot the standalone runtime executable and stay in the traditional game
// loop until the engine requests quit.
int RuntimeApp::Run() {
    std::ios::sync_with_stdio(false);
    Engine engine;
    engine.GameLoop();
    return 0;
}
