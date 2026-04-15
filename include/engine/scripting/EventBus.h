#ifndef EVENTBUS_H
#define EVENTBUS_H

#include <string>

struct lua_State;

namespace luabridge {
class LuaException;
class LuaRef;
}

namespace EventBus {

struct Hooks {
    // lua state used by EventBus, mainly for stack cleanup after errors
    // EventBus 使用的 lua state, 主要用于异常后的栈清理
    lua_State *lua_state = nullptr;

    // compare whether two LuaRef objects point to the same Lua object
    // 判断两个 LuaRef 是否指向同一个 Lua 对象
    bool (*are_same_ref)(const luabridge::LuaRef &,
                         const luabridge::LuaRef &) = nullptr;

    // extract a stable component identity from a component ref
    // 从组件引用中提取稳定身份, 即 actor_id 和 component_key
    bool (*try_extract_component_identity)(const luabridge::LuaRef &, int &,
                                           std::string &) = nullptr;

    // check whether a component ref is still alive
    // 判断组件引用当前是否仍然有效
    bool (*is_component_ref_alive)(const luabridge::LuaRef &, int,
                                   const std::string &) = nullptr;

    // host-side error reporter for Lua callback failures
    // Lua 回调报错时交给宿主侧输出
    void (*report_error)(int actor_id,
                         const luabridge::LuaException &) = nullptr;
};

// initialize hooks and clear old subscription state
// 初始化 hooks, 并清空旧的订阅状态
void Initialize(const Hooks &hooks);

// clear subscriptions and pending operations, but keep hooks
// 清空订阅和待处理操作, 但保留 hooks
void Clear();

// fully reset EventBus, including hooks
// 完整重置 EventBus, 包括 hooks
void Shutdown();

// publish immediately to current active subscribers
// 立即发布给当前已经生效的订阅者
void Publish(const std::string &event_type, luabridge::LuaRef event_object);

// queued subscription operations become visible only when applied
// 订阅和取消订阅都会先进入待处理队列, 统一 apply 后才生效
void Subscribe(const std::string &event_type, luabridge::LuaRef component_ref,
               luabridge::LuaRef function_ref);
void Unsubscribe(const std::string &event_type, luabridge::LuaRef component_ref,
                 luabridge::LuaRef function_ref);

// remove both active and pending subscriptions for one component
// 移除某个组件已有的和待生效的所有订阅
void RemoveSubscriptionsForComponent(const luabridge::LuaRef &component_ref,
                                     int actor_id,
                                     const std::string &component_key);

// apply queued subscribe / unsubscribe operations
// 统一应用排队中的订阅变更
void ApplyPendingOperations();

} // namespace EventBus

#endif
