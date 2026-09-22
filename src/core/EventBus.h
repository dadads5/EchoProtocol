// EventBus.h —— 类型安全的事件总线（M2）
//
// 设计要点（对应 GDD 第 6 节）：
//   订阅主体是「系统」（System），不是实体。
//   碰撞系统只负责 publish(TrapHitEvent)，不关心谁处理；
//   循环系统（LoopSystem）订阅 PlayerDiedEvent，负责重置。
//   两边零耦合 —— 加一个新系统不需要改任何已有代码。
//
// 用法：
//   auto id = bus.subscribe<PlayerDiedEvent>([this](const PlayerDiedEvent& e){ ... });
//   bus.publish(PlayerDiedEvent{ ... });
//   bus.unsubscribe(id);                       // 也可以退订
//   bus.subscriberCount<PlayerDiedEvent>();    // 单测用
//
// 事件类型必须是可拷贝的普通结构体（POD 风格），不要放裸指针。
//
// 实现说明：
//   用 type_index 做键，把每种事件的订阅者放在各自的 vector 里，
//   类型擦除只发生在内部（std::function<void(const void*)>），
//   对外始终是强类型的 —— 编译期就杜绝了「订阅 A 事件却收到 B 事件」。
#pragma once

#include <cstddef>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace echo {

class EventBus {
public:
    using HandlerId = std::size_t;

    // 订阅事件 E。F 需满足可调用 f(const E&)。
    template <typename E, typename F>
    HandlerId subscribe(F&& fn) {
        const HandlerId id = ++m_nextId;

        Entry entry;
        entry.id = id;
        entry.invoke = [cb = std::forward<F>(fn)](const void* payload) {
            cb(*static_cast<const E*>(payload));
        };

        m_handlers[std::type_index(typeid(E))].push_back(std::move(entry));
        return id;
    }

    void unsubscribe(HandlerId id) {
        for (auto& kv : m_handlers) {
            auto& list = kv.second;
            for (auto it = list.begin(); it != list.end(); ++it) {
                if (it->id == id) {
                    list.erase(it);
                    return;
                }
            }
        }
    }

    // 派发事件。注意：这里是同步派发。
    template <typename E>
    void publish(const E& event) const {
        const auto it = m_handlers.find(std::type_index(typeid(E)));
        if (it == m_handlers.end()) return;

        // 先拷贝一份订阅者列表再回调：
        // 允许回调内部继续订阅/退订，不会让迭代器失效。
        const std::vector<Entry> snapshot = it->second;
        for (const Entry& e : snapshot) {
            e.invoke(&event);
        }
    }

    template <typename E>
    std::size_t subscriberCount() const {
        const auto it = m_handlers.find(std::type_index(typeid(E)));
        return it == m_handlers.end() ? 0u : it->second.size();
    }

    void clear() {
        m_handlers.clear();
        m_nextId = 0;
    }

private:
    struct Entry {
        HandlerId                        id = 0;
        std::function<void(const void*)> invoke;
    };

    std::unordered_map<std::type_index, std::vector<Entry>> m_handlers;
    HandlerId                                               m_nextId = 0;
};

} // namespace echo
