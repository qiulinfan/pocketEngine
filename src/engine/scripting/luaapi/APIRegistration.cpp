#include "scripting/APIRegistration.h"
#include "RegistrationDetail.h"

namespace APIRegistrationDetail {

lua_State *g_lua_state = nullptr;
Engine *g_engine = nullptr;

} // namespace APIRegistrationDetail

namespace APIRegistration {

// register host APIs and bindings into Lua
void Initialize(lua_State *lua_state) {
    using namespace APIRegistrationDetail;

    g_lua_state = lua_state;
    RegisterApplicationAndDebugAPI();
    RegisterSceneAndCameraAPI();
    RegisterAudioAPI();
    RegisterRenderingAPI();
    RegisterInputAPI();
    RegisterActorAPI();
    RegisterEventAPI();
    RegisterPhysicsAPI();
    RegisterBuiltinComponentAPI();
}

// update the current engine pointer used by Lua APIs
void BindEngine(Engine *engine) {
    APIRegistrationDetail::g_engine = engine;
}

} // namespace APIRegistration
