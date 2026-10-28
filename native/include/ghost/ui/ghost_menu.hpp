#pragma once
#include <stdexcept>
#include <string>
#include <array>
#include <vector>
#include "ghost/visual/ghost_tag.hpp"
#include "ghost/ui/menu_layout.hpp"

namespace ghost
{
class ModMenu
{
    ue::Api& api;
    ue::Reflection& reflection;
    ue::Weak widget, owner;
    std::string caption;
    int page{};
    std::array<std::string, 7> peer_ids{}, labels{};
    int configured_page{-1};

    void text(ue::Object* target, const std::string& value)
    {
        auto wide = ue::wide(value);
        ue::String string{wide.data(), static_cast<int>(wide.size() + 1), static_cast<int>(wide.size() + 1)};
        ue::Reflection::Call convert(reflection, L"/Script/Engine.KismetTextLibrary:Conv_StringToText");
        convert.set(L"InString", string).run(api.require(L"/Script/Engine.Default__KismetTextLibrary"));
        try
        {
            ue::Reflection::Call set(reflection, L"/Script/UMG.TextBlock:SetText");
            set.copy(L"InText", convert, L"ReturnValue");
            set.run(target);
        }
        catch (...) { convert.destroy(L"ReturnValue"); throw; }
        convert.destroy(L"ReturnValue");
    }
    std::string input(const wchar_t* name)
    {
        auto* object = api.weak_get(&widget);
        ue::Reflection::Call get(reflection, L"/Script/UMG.EditableTextBox:GetText");
        get.run(api.object_property(object, name));
        ue::Reflection::Call convert(reflection, L"/Script/Engine.KismetTextLibrary:Conv_TextToString");
        bool converted = false;
        std::string result;
        try
        {
            convert.copy(L"InText", get, L"ReturnValue");
            convert.run(api.require(L"/Script/Engine.Default__KismetTextLibrary"));
            converted = true;
            const auto value = convert.get<ue::String>();
            if (value.count > 257 || value.count < 0)
                throw std::runtime_error("Menu field is too long");
            if (value.count > 0 && value.count <= 257 && value.data)
                result = ue::utf8(std::wstring(value.data, static_cast<std::size_t>(value.count - 1)));
        }
        catch (...)
        {
            if (converted)
                convert.destroy(L"ReturnValue");
            get.destroy(L"ReturnValue");
            throw;
        }
        convert.destroy(L"ReturnValue");
        get.destroy(L"ReturnValue");
        if (result.find_first_of("\t\r\n") != result.npos)
            throw std::runtime_error("Menu fields must not contain tabs or line breaks");
        return result;
    }
    void visible(ue::Object* object, const std::wstring& name, bool show)
    {
        ue::Reflection::Call call(reflection, L"/Script/UMG.Widget:SetVisibility");
        call.set(L"InVisibility", static_cast<unsigned char>(show ? 0 : 1))
            .run(api.object_property(object, name.c_str()));
    }
    void property(ue::Object* target, const wchar_t* name, bool value)
    {
        ue::Name id;
        api.name_ctor(&id, name, 1, nullptr);
        ue::Reflection::Call set(reflection,
                                 L"/Script/Engine.KismetSystemLibrary:SetBoolPropertyByName");
        set.set(L"Object", target)
            .set(L"PropertyName", id)
            .set(L"Value", value)
            .run(api.require(L"/Script/Engine.Default__KismetSystemLibrary"));
    }

public:
    std::string selected_peer, network_command;
    bool invite_requested{};
    ModMenu(ue::Api& a, ue::Reflection& r) : api(a), reflection(r) {}
    bool open() const
    {
        return api.weak_get(&widget) != nullptr;
    }
    void close()
    {
        if (auto* controller = api.weak_get(&owner))
        {
            ue::Reflection::Call mode(reflection,
                                      L"/Script/UMG.WidgetBlueprintLibrary:SetInputMode_GameOnly");
            mode.set(L"PlayerController", controller)
                .run(api.require(L"/Script/UMG.Default__WidgetBlueprintLibrary"));
            property(controller, L"bShowMouseCursor", false);
        }
        owner = {};
        if (auto* object = api.weak_get(&widget))
        {
            ue::Reflection::Call remove(reflection, L"/Script/UMG.Widget:RemoveFromParent");
            remove.run(object);
        }
        widget = {};
        caption.clear();
        configured_page = -1;
        labels = {};
        peer_ids = {};
    }
    void toggle(ue::Object* controller)
    {
        if (open())
        {
            close();
            return;
        }
        // Do not take focus from an existing game menu, loading or a cutscene.
        ue::Reflection::Call ignored(reflection, L"/Script/Engine.Controller:IsMoveInputIgnored");
        ignored.run(controller);
        ue::Reflection::Call paused(reflection, L"/Script/Engine.GameplayStatics:IsGamePaused");
        paused.set(L"WorldContextObject", controller)
            .run(api.require(L"/Script/Engine.Default__GameplayStatics"));
        if (ignored.get<bool>() || paused.get<bool>())
            return;
        auto* type = api.load(L"/Game/Mods/AHGhostCoopNavigation/WBP_GhostMenu.WBP_GhostMenu_C");
        ue::Reflection::Call create(reflection, L"/Script/UMG.WidgetBlueprintLibrary:Create");
        create.set(L"WorldContextObject", controller)
            .set(L"WidgetType", type)
            .set(L"OwningPlayer", controller)
            .run(api.require(L"/Script/UMG.Default__WidgetBlueprintLibrary"));
        auto* object = create.get<ue::Object*>();
        if (!object)
            throw std::runtime_error("mod menu creation failed");
        widget = api.weak(object);
        owner = api.weak(controller);
        ue::Reflection::Call add(reflection, L"/Script/UMG.UserWidget:AddToViewport");
        add.set(L"ZOrder", 500).run(object);
        ue::Reflection::Call size(reflection, L"/Script/UMG.UserWidget:SetDesiredSizeInViewport");
        size.set(L"Size", Vec2{600, 640}).run(object);
        ue::Reflection::Call align(reflection, L"/Script/UMG.UserWidget:SetAlignmentInViewport");
        align.set(L"Alignment", Vec2{.5f, .5f}).run(object);
        ue::Reflection::Call viewport(reflection,
                                      L"/Script/Engine.PlayerController:GetViewportSize");
        viewport.run(controller);
        const float width = static_cast<float>(viewport.get<int>(L"SizeX")),
                    height = static_cast<float>(viewport.get<int>(L"SizeY"));
        ue::Reflection::Call dpi(reflection, L"/Script/UMG.WidgetLayoutLibrary:GetViewportScale");
        dpi.set(L"WorldContextObject", controller)
            .run(api.require(L"/Script/UMG.Default__WidgetLayoutLibrary"));
        const float scale = dpi.get<float>();
        const auto fit = menu_render_scale(width, height, scale);
        if (!fit)
            throw std::runtime_error("menu viewport unavailable");
        ue::Reflection::Call pivot(reflection, L"/Script/UMG.Widget:SetRenderTransformPivot");
        pivot.set(L"Pivot", Vec2{.5f, .5f}).run(object);
        ue::Reflection::Call render_scale(reflection, L"/Script/UMG.Widget:SetRenderScale");
        render_scale.set(L"Scale", Vec2{*fit, *fit}).run(object);
        ue::Reflection::Call position(reflection, L"/Script/UMG.UserWidget:SetPositionInViewport");
        position
            .set(L"Position",
                 Vec2{viewport.get<int>(L"SizeX") * .5f, viewport.get<int>(L"SizeY") * .5f})
            .set(L"bRemoveDPIScale", true)
            .run(object);
        ue::Reflection::Call mode(reflection,
                                  L"/Script/UMG.WidgetBlueprintLibrary:SetInputMode_UIOnlyEx");
        mode.set(L"PlayerController", controller)
            .set(L"InWidgetToFocus", api.object_property(object, L"Action21"))
            .set(L"InMouseLockMode", static_cast<unsigned char>(0))
            .run(api.require(L"/Script/UMG.Default__WidgetBlueprintLibrary"));
        property(controller, L"bShowMouseCursor", true);
    }
    int update(const std::string& status, const Roster& roster, const std::string& world, double now)
    {
        selected_peer.clear();
        network_command.clear();
        invite_requested = false;
        auto* object = api.weak_get(&widget);
        if (!object)
            return 0;
        const int request = api.read_property<int>(object, L"Request");
        if (request)
        {
            ue::Name id;
            api.name_ctor(&id, L"Request", 1, nullptr);
            ue::Reflection::Call clear(reflection, L"/Script/Engine.KismetSystemLibrary:SetIntPropertyByName");
            clear.set(L"Object", object).set(L"PropertyName", id).set(L"Value", 0)
                .run(api.require(L"/Script/Engine.Default__KismetSystemLibrary"));
        }
        if (request == 1) { close(); return 1; }
        if (request >= 21 && request <= 23)
            page = request - 21;
        int result = 0;
        if (request >= 100 && request <= 106)
        {
            const auto index = static_cast<std::size_t>(request - 100);
            if (page == 2 && !peer_ids[index].empty())
            {
                selected_peer = peer_ids[index];
                result = 6;
            }
            else if (page == 0 || page == 1)
            {
                if (page == 0 && index == 2)
                    invite_requested = true;
                else if (page == 0 && index == 4)
                    result = 5;
                else
                {
                    std::string verb;
                    if (index == 0) verb = page == 0 ? "steam_host" : "lan_host";
                    if (index == 1) verb = page == 0 ? "steam_join" : "lan_join";
                    if ((page == 0 && index == 3) || (page == 1 && index == 2)) verb = "disconnect";
                    if (!verb.empty())
                        network_command = verb + "\t" + input(L"NameInput") + "\t" +
                                          input(L"RoomInput") + "\t" + input(L"AddressInput");
                }
            }
        }
        if (caption != status)
        {
            text(api.object_property(object, L"StatusText"), status);
            caption = status;
        }
        if (configured_page != page)
        {
            visible(object, L"Inputs", page != 2);
            text(api.object_property(object, L"PageTitle"), page == 0 ? "Steam Spacewar" :
                 page == 1 ? "Локальная сеть" : "Телепортация — выберите игрока");
            configured_page = page;
        }
        std::array<std::string, 7> next{};
        std::array<bool, 7> enabled{};
        peer_ids = {};
        if (page == 0)
        {
            next = {"Создать лобби", "Подключиться по ID лобби", "Пригласить друга — Steam Overlay",
                    "Покинуть лобби", "Показать / скрыть игроков", "", ""};
            enabled = {true, true, true, true, true, false, false};
        }
        else if (page == 1)
        {
            next = {"Создать LAN-сервер", "Подключиться к LAN-серверу", "Отключиться", "", "", "", ""};
            enabled = {true, true, true, false, false, false, false};
        }
        else
        {
            std::size_t index = 0;
            for (const auto& peer : roster.players)
            {
                if (index >= next.size()) break;
                peer_ids[index] = peer.id;
                enabled[index] = peer.world == world && now >= peer.received_at && now - peer.received_at <= .75;
                next[index] = peer.name + (peer.world != world ? " — другая карта" :
                                          !enabled[index] ? " — нет свежих координат" : "");
                ++index;
            }
            if (!index) next[0] = "В лобби пока нет других игроков";
        }
        for (std::size_t index = 0; index < next.size(); ++index)
        {
            const auto suffix = std::to_wstring(index);
            if (labels[index] != next[index] || configured_page == -1)
                text(api.object_property(object, (L"ItemLabel" + suffix).c_str()), next[index]);
            visible(object, L"ItemRow" + suffix, !next[index].empty());
            ue::Reflection::Call enable(reflection, L"/Script/UMG.Widget:SetIsEnabled");
            enable.set(L"bInIsEnabled", enabled[index])
                .run(api.object_property(object, (L"Item" + suffix).c_str()));
            labels[index] = next[index];
        }
        return result;
    }
};
} // namespace ghost
