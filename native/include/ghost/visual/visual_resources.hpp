#pragma once
#include <functional>
#include <string>
#include "ghost/engine/unreal_api.hpp"
#include <map>
#include <string_view>

namespace ghost
{
// Shared weak asset cache and component setup for body and weapon renderers.
class VisualResources
{
    using Call = ue::Reflection::Call;
    ue::Api& api;
    ue::Reflection& reflection;
    std::map<std::wstring, ue::Weak, std::less<>> assets;

public:
    VisualResources(ue::Api& a, ue::Reflection& r) : api(a), reflection(r) {}
    ue::Object* asset(std::wstring_view path)
    {
        auto& handle = assets[std::wstring(path)];
        if (auto* value = api.weak_get(&handle))
            return value;
        auto* value = api.load(path.data());
        handle = api.weak(value);
        return value;
    }
    void boolean(ue::Object* target, const wchar_t* path, const wchar_t* field, bool value)
    {
        Call call(reflection, path);
        call.set(field, value).run(target);
    }
    void mobility(ue::Object* component)
    {
        Call call(reflection, L"/Script/Engine.SceneComponent:SetMobility");
        call.set(L"NewMobility", static_cast<unsigned char>(2)).run(component);
    }
};
} // namespace ghost
