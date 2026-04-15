#ifndef APIREGISTRATION_H
#define APIREGISTRATION_H

struct lua_State;
class Engine;

namespace APIRegistration {

// register host APIs and bindings into Lua
// 向 Lua 注册宿主 API 和绑定
void Initialize(lua_State *lua_state);

// update the current engine pointer used by Lua APIs
// 更新 Lua API 使用的当前引擎指针
void BindEngine(Engine *engine);

} // namespace APIRegistration

#endif
