#ifndef API_REGISTRATION_DETAIL_H
#define API_REGISTRATION_DETAIL_H

struct lua_State;
class Engine;

namespace APIRegistrationDetail {

extern lua_State *g_lua_state;
extern Engine *g_engine;

void RegisterApplicationAndDebugAPI();
void RegisterSceneAndCameraAPI();
void RegisterAudioAPI();
void RegisterRenderingAPI();
void RegisterInputAPI();
void RegisterActorAPI();
void RegisterEventAPI();
void RegisterPhysicsAPI();
void RegisterBuiltinComponentAPI();

} // namespace APIRegistrationDetail

#endif
