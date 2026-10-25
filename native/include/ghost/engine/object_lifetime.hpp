#pragma once
#include <utility>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>

namespace ue
{
struct Object;
struct Weak
{
    std::shared_ptr<std::atomic<Object*>> value;
};

class ObjectLifetime
{
    std::mutex mutex;
    std::map<const Object*, std::weak_ptr<std::atomic<Object*>>> handles;

public:
    Weak track(Object* object)
    {
        if (!object)
            return {};
        std::scoped_lock lock(mutex);
        auto& entry = handles[object];
        auto value = entry.lock();
        if (!value)
        {
            value = std::make_shared<std::atomic<Object*>>(object);
            entry = value;
        }
        return {std::move(value)};
    }
    static Object* get(const Weak& handle)
    {
        return handle.value ? handle.value->load(std::memory_order_acquire) : nullptr;
    }
    void forget(const Object* object)
    {
        std::scoped_lock lock(mutex);
        auto it = handles.find(object);
        if (it == handles.end())
            return;
        if (auto value = it->second.lock())
            value->store(nullptr, std::memory_order_release);
        handles.erase(it);
    }
    void clear()
    {
        std::scoped_lock lock(mutex);
        for (const auto& [object, entry] : handles)
        {
            (void)object;
            if (auto value = entry.lock())
                value->store(nullptr, std::memory_order_release);
        }
        handles.clear();
    }
};

// Same three virtual entries as UE4SS's FUObjectDeleteListener / UE4.27.
struct DeleteListener
{
    virtual ~DeleteListener() = default;
    virtual void NotifyUObjectDeleted(const Object*, int) = 0;
    virtual void OnUObjectArrayShutdown() = 0;
};
} // namespace ue
