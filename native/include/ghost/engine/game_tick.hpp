#pragma once
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include "ghost/engine/unreal_api.hpp"
#include "vendor/minhook-1.3.4/include/MinHook.h"

// UE4SS cce0ed64 resolves UEngine's pure-virtual stub. Derive the concrete
// UGameEngine override from the same slot, without a fixed game address/offset.
class GameTickHook
{
    using Tick = void (*)(ue::Object*, float, bool);
    inline static GameTickHook* active{};
    Tick original{};
    void* target{};
    void* owner{};
    void (*callback)(void*, ue::Object*){};
    static bool executable(void* address)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(address, &region, sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;
        return (region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                  PAGE_EXECUTE_WRITECOPY)) != 0;
    }
    static void detour(ue::Object* engine, float delta, bool idle)
    {
        auto* hook = active;
        hook->original(engine, delta, idle);
        if (!idle)
            hook->callback(hook->owner, engine);
    }

public:
    std::size_t slot{};
    void install(ue::Api& api, void* context, void (*after)(void*, ue::Object*))
    {
        if (target)
            return;
        auto* internal = GetProcAddress(
            GetModuleHandleW(L"UE4SS.dll"),
            "?TickInternal@UEngine@Unreal@RC@@2V?$Function@$$A6AXPEAVUEngine@Unreal@RC@@M@Z@3@A");
        if (!internal)
            throw std::runtime_error("UE4SS engine tick symbol missing");
        void* base_tick{};
        // First member of this build's RC::Unreal::Function is the resolved address.
        std::memcpy(&base_tick, internal, sizeof(base_tick));
        if (!executable(base_tick))
            throw std::runtime_error("UE4SS base engine tick was not resolved");
        auto** base = *reinterpret_cast<void***>(api.require(L"/Script/Engine.Default__Engine"));
        auto** concrete =
            *reinterpret_cast<void***>(api.require(L"/Script/Engine.Default__GameEngine"));
        bool found = false;
        for (slot = 0; slot < 256; ++slot)
        {
            if (!executable(base[slot]))
                break; // Stop at the RTTI boundary of the next vtable.
            if (base[slot] == base_tick)
            {
                found = true;
                break;
            }
        }
        if (!found)
            throw std::runtime_error("UEngine::Tick slot not found in base vtable");
        void* resolved = concrete[slot];
        if (!executable(resolved) || resolved == base_tick)
            throw std::runtime_error("UGameEngine::Tick override is not available");
        MH_STATUS result = MH_Initialize();
        if (result != MH_OK && result != MH_ERROR_ALREADY_INITIALIZED)
            throw std::runtime_error(MH_StatusToString(result));
        owner = context;
        callback = after;
        active = this;
        result = MH_CreateHook(resolved, reinterpret_cast<void*>(&detour),
                               reinterpret_cast<void**>(&original));
        if (result != MH_OK)
            throw std::runtime_error(MH_StatusToString(result));
        result = MH_EnableHook(resolved);
        if (result != MH_OK)
        {
            MH_RemoveHook(resolved);
            throw std::runtime_error(MH_StatusToString(result));
        }
        target = resolved;
    }
    void stop()
    {
        if (!target)
            return;
        MH_DisableHook(target);
        MH_RemoveHook(target);
        target = nullptr;
    }
};
