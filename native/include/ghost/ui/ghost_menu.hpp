#pragma once
#include <stdexcept>
#include <string>
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
            .set(L"InWidgetToFocus", api.object_property(object, L"Action1"))
            .set(L"InMouseLockMode", static_cast<unsigned char>(0))
            .run(api.require(L"/Script/UMG.Default__WidgetBlueprintLibrary"));
        property(controller, L"bShowMouseCursor", true);
    }
    int update(const std::string& status)
    {
        auto* object = api.weak_get(&widget);
        if (!object)
            return 0;
        if (caption != status)
        {
            auto wide = ue::wide(status);
            ue::String string{wide.data(), static_cast<int>(wide.size() + 1),
                              static_cast<int>(wide.size() + 1)};
            ue::Reflection::Call text(reflection,
                                      L"/Script/Engine.KismetTextLibrary:Conv_StringToText");
            text.set(L"InString", string)
                .run(api.require(L"/Script/Engine.Default__KismetTextLibrary"));
            try
            {
                ue::Reflection::Call set(reflection, L"/Script/UMG.TextBlock:SetText");
                set.copy(L"InText", text, L"ReturnValue");
                set.run(api.object_property(object, L"StatusText"));
            }
            catch (...)
            {
                text.destroy(L"ReturnValue");
                throw;
            }
            text.destroy(L"ReturnValue");
            caption = status;
        }
        const int request = api.read_property<int>(object, L"Request");
        if (request)
        {
            ue::Name id;
            api.name_ctor(&id, L"Request", 1, nullptr);
            ue::Reflection::Call clear(reflection,
                                       L"/Script/Engine.KismetSystemLibrary:SetIntPropertyByName");
            clear.set(L"Object", object)
                .set(L"PropertyName", id)
                .set(L"Value", 0)
                .run(api.require(L"/Script/Engine.Default__KismetSystemLibrary"));
        }
        if (request == 1)
            close();
        return request == 1 || request == 5 || request == 6 ? request : 0;
    }
};
} // namespace ghost
