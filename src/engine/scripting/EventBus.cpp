#include "scripting/EventBus.h"
#include "lua.hpp"
#include "LuaBridge/LuaBridge.h"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// 一个已生效的订阅项
// 某个 component 的某个 Lua function 正在订阅某种 event_type
struct EventSubscription {
    luabridge::LuaRef component_ref;
    luabridge::LuaRef function_ref;
    int actor_id = -1;
    std::string component_key;

    EventSubscription(luabridge::LuaRef component_ref_in,
                      luabridge::LuaRef function_ref_in, int actor_id_in,
                      std::string component_key_in)
        : component_ref(std::move(component_ref_in)),
          function_ref(std::move(function_ref_in)), actor_id(actor_id_in),
          component_key(std::move(component_key_in)) {}
};

// Subscribe / Unsubscribe 不会立刻改动正式订阅表
// 而是先排入这个待生效队列, 等帧末统一应用
struct PendingEventOperation {
    bool is_subscribe = true;
    std::string event_type;
    luabridge::LuaRef component_ref;
    luabridge::LuaRef function_ref;
    int actor_id = -1;
    std::string component_key;

    PendingEventOperation(bool is_subscribe_in, std::string event_type_in,
                          luabridge::LuaRef component_ref_in,
                          luabridge::LuaRef function_ref_in, int actor_id_in,
                          std::string component_key_in)
        : is_subscribe(is_subscribe_in), event_type(std::move(event_type_in)),
          component_ref(std::move(component_ref_in)),
          function_ref(std::move(function_ref_in)), actor_id(actor_id_in),
          component_key(std::move(component_key_in)) {}
};

EventBus::Hooks g_hooks;
std::unordered_map<std::string, std::vector<EventSubscription>>
    g_event_subscriptions;
std::vector<PendingEventOperation> g_pending_event_operations;


bool AreSameRef(const luabridge::LuaRef &a, const luabridge::LuaRef &b) {
    if (g_hooks.are_same_ref == nullptr) return false;
    return g_hooks.are_same_ref(a, b);
}

// 把一个 component LuaRef 解析为稳定身份: actor_id + component_key
bool TryExtractComponentIdentity(const luabridge::LuaRef &component_ref,
                                 int &actor_id, std::string &component_key) {
    if (g_hooks.try_extract_component_identity == nullptr) return false;
    return g_hooks.try_extract_component_identity(component_ref, actor_id,
                                                  component_key);
}

// 订阅表里可能残留已经被销毁的 component, 需要通过外部查询其存活性
bool IsComponentRefAlive(const luabridge::LuaRef &component_ref, int actor_id,
                         const std::string &component_key) {
    if (g_hooks.is_component_ref_alive == nullptr) return false;
    return g_hooks.is_component_ref_alive(component_ref, actor_id,
                                          component_key);
}

// EventBus 统一捕获 Lua 回调异常, 并交回宿主侧格式化输出
void ReportError(int actor_id, const luabridge::LuaException &e) {
    if (g_hooks.report_error != nullptr) {
        g_hooks.report_error(actor_id, e);
    }
    if (g_hooks.lua_state != nullptr) {
        lua_settop(g_hooks.lua_state, 0);
    }
}

// 一个订阅是否对应这个 component-function pair
bool DoesSubscriptionMatch(const EventSubscription &subscription,
                           const luabridge::LuaRef &component_ref,
                           const luabridge::LuaRef &function_ref, int actor_id,
                           const std::string &component_key) {
    if (subscription.actor_id != actor_id) return false;
    if (subscription.component_key != component_key) return false;
    if (!AreSameRef(subscription.component_ref, component_ref)) return false;
    return AreSameRef(subscription.function_ref, function_ref);
}

} // namespace

namespace EventBus {

// initialize hooks and clear old subscription state
void Initialize(const Hooks &hooks) {
    g_hooks = hooks;
    Clear();
}

// clear subscriptions and pending operations, but keep hooks
void Clear() {
    g_event_subscriptions.clear();
    g_pending_event_operations.clear();
}

// fully reset EventBus, including hooks
void Shutdown() {
    Clear();
    g_hooks = Hooks();
}

// publish immediately to current active subscribers
void Publish(const std::string &event_type, luabridge::LuaRef event_object) {
    // Publish 是立即分发的: 只读取“当前已生效”的订阅表
    auto subscriptions_it = g_event_subscriptions.find(event_type);
    if (subscriptions_it == g_event_subscriptions.end()) return;

    std::vector<EventSubscription> &subscriptions = subscriptions_it->second;
    // 先顺手清掉已经失效的订阅, 避免把事件投给被销毁的 component
    subscriptions.erase(
        std::remove_if(subscriptions.begin(), subscriptions.end(),
                       [](const EventSubscription &subscription) {
                           return !IsComponentRefAlive(
                               subscription.component_ref, subscription.actor_id,
                               subscription.component_key);
                       }),
        subscriptions.end());

    if (subscriptions.empty()) {
        g_event_subscriptions.erase(subscriptions_it);
        return;
    }

    // 用快照分发, 避免 callback 内再次 Publish / Unsubscribe 时
    // 直接打乱当前这次派发的遍历
    const std::vector<EventSubscription> subscribers = subscriptions;
    for (const EventSubscription &subscription : subscribers) {
        if (!IsComponentRefAlive(subscription.component_ref, subscription.actor_id,
                                 subscription.component_key)) {
            continue;
        }

        try {
            subscription.function_ref(subscription.component_ref, event_object);
        } catch (const luabridge::LuaException &e) {
            ReportError(subscription.actor_id, e);
        }
    }
}

// queued subscription operations become visible only when applied
void Subscribe(const std::string &event_type, luabridge::LuaRef component_ref,
               luabridge::LuaRef function_ref) {
    // 只有合法 Lua function 才能被订阅
    if (!function_ref.isFunction()) return;

    int actor_id = -1;
    std::string component_key;
    if (!TryExtractComponentIdentity(component_ref, actor_id, component_key)) {
        return;
    }

    g_pending_event_operations.emplace_back(true, event_type, component_ref,
                                            function_ref, actor_id,
                                            component_key);
}

// queued subscription operations become visible only when applied
void Unsubscribe(const std::string &event_type, luabridge::LuaRef component_ref,
                 luabridge::LuaRef function_ref) {
    if (!function_ref.isFunction()) return;

    int actor_id = -1;
    std::string component_key;
    if (!TryExtractComponentIdentity(component_ref, actor_id, component_key)) {
        return;
    }

    g_pending_event_operations.emplace_back(false, event_type, component_ref,
                                            function_ref, actor_id,
                                            component_key);
}

// remove both active and pending subscriptions for one component
void RemoveSubscriptionsForComponent(const luabridge::LuaRef &component_ref,
                                     int actor_id,
                                     const std::string &component_key) {
    for (auto subscriptions_it = g_event_subscriptions.begin();
         subscriptions_it != g_event_subscriptions.end();) {
        std::vector<EventSubscription> &subscriptions = subscriptions_it->second;
        subscriptions.erase(
            std::remove_if(subscriptions.begin(), subscriptions.end(),
                           [&](const EventSubscription &subscription) {
                               return DoesSubscriptionMatch(
                                   subscription, component_ref,
                                   subscription.function_ref, actor_id,
                                   component_key);
                           }),
            subscriptions.end());

        if (subscriptions.empty()) {
            subscriptions_it = g_event_subscriptions.erase(subscriptions_it);
        } else {
            ++subscriptions_it;
        }
    }

    g_pending_event_operations.erase(
        std::remove_if(
            g_pending_event_operations.begin(), g_pending_event_operations.end(),
            [&](const PendingEventOperation &operation) {
                return operation.actor_id == actor_id &&
                       operation.component_key == component_key &&
                       AreSameRef(operation.component_ref, component_ref);
            }),
        g_pending_event_operations.end());
}

// apply queued subscribe / unsubscribe operations
void ApplyPendingOperations() {
    if (g_pending_event_operations.empty()) return;

    std::vector<PendingEventOperation> pending_operations = std::move(g_pending_event_operations);
    g_pending_event_operations.clear();

    for (const PendingEventOperation &operation : pending_operations) {
        if (operation.event_type.empty()) continue;

        auto subscriptions_it = g_event_subscriptions.find(operation.event_type);
        if (!operation.is_subscribe) {
            if (subscriptions_it == g_event_subscriptions.end()) continue;

            std::vector<EventSubscription> &subscriptions = subscriptions_it->second;
            subscriptions.erase(
                std::remove_if(
                    subscriptions.begin(), subscriptions.end(),
                    [&](const EventSubscription &subscription) {
                        return DoesSubscriptionMatch(
                            subscription, operation.component_ref,
                            operation.function_ref, operation.actor_id,
                            operation.component_key);
                    }),
                subscriptions.end());

            if (subscriptions.empty()) {
                g_event_subscriptions.erase(subscriptions_it);
            }
            continue;
        }

        // Subscribe 生效时再次检查 component 是否还活着
        if (!operation.function_ref.isFunction()) continue;
        if (!IsComponentRefAlive(operation.component_ref, operation.actor_id,
                                 operation.component_key)) {
            continue;
        }

        std::vector<EventSubscription> &subscriptions = g_event_subscriptions[operation.event_type];
        // 同一个 component-function pair 只保留一份正式订阅
        const bool already_subscribed = std::any_of(
            subscriptions.begin(), subscriptions.end(),
            [&](const EventSubscription &subscription) {
                return DoesSubscriptionMatch(
                    subscription, operation.component_ref, operation.function_ref,
                    operation.actor_id, operation.component_key);
            });
        if (already_subscribed) continue;

        subscriptions.emplace_back(operation.component_ref, operation.function_ref,
                                   operation.actor_id, operation.component_key);
    }
}

} // namespace EventBus
