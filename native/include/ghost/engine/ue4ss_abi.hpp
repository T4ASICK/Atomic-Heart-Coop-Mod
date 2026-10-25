#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace RC
{
using StringType = std::wstring;
using StringViewType = std::wstring_view;

namespace GUI
{
class GUITab;
}

namespace LuaMadeSimple
{
class Lua;
}

// UE4SS 3.0.1 C++ user-mod ABI. Keeping this local avoids linking a mod
// against a different UE4SS build while preserving the loader-facing layout.
class CppUserModBase
{
protected:
    std::vector<std::shared_ptr<GUI::GUITab>> GUITabs{};

public:
    StringType ModName{};
    StringType ModVersion{};
    StringType ModDescription{};
    StringType ModAuthors{};
    StringType ModIntendedSDKVersion{};

    CppUserModBase() = default;
    virtual ~CppUserModBase() = default;

    virtual auto on_update() -> void {}
    virtual auto on_unreal_init() -> void {}
    virtual auto on_program_start() -> void {}

    virtual auto on_lua_start(StringViewType, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&,
                              LuaMadeSimple::Lua&, std::vector<LuaMadeSimple::Lua*>&) -> void
    {
    }

    virtual auto on_lua_start(LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&,
                              std::vector<LuaMadeSimple::Lua*>&) -> void
    {
    }

    virtual auto on_lua_stop(StringViewType, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&,
                             LuaMadeSimple::Lua&, std::vector<LuaMadeSimple::Lua*>&) -> void
    {
    }

    virtual auto on_lua_stop(LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&,
                             std::vector<LuaMadeSimple::Lua*>&) -> void
    {
    }

    virtual auto on_dll_load(std::wstring_view) -> void {}
    virtual auto render_tab() -> void {}
};
} // namespace RC
