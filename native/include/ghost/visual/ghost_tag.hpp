#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include "ghost/core/state.hpp"
#include "ghost/engine/unreal_api.hpp"

namespace ghost
{
struct Vec2
{
    float x{}, y{};
};
struct Tag
{
    ue::Weak widget, text, distance;
    std::string caption, distance_caption;
};
class Tags
{
    ue::Api& api;
    ue::Reflection& reflection;
    ue::Weak widget_class;
    using Call = ue::Reflection::Call;
    void text(ue::Object* object, const std::string& value)
    {
        std::wstring wide = ue::wide(value);
        ue::String string{wide.data(), static_cast<int>(wide.size() + 1),
                          static_cast<int>(wide.size() + 1)};
        Call convert(reflection, L"/Script/Engine.KismetTextLibrary:Conv_StringToText");
        convert.set(L"InString", string)
            .run(api.require(L"/Script/Engine.Default__KismetTextLibrary"));
        try
        {
            Call set(reflection, L"/Script/UMG.TextBlock:SetText");
            set.copy(L"InText", convert, L"ReturnValue");
            set.run(object);
        }
        catch (...)
        {
            convert.destroy(L"ReturnValue");
            throw;
        }
        convert.destroy(L"ReturnValue");
    }

public:
    Tags(ue::Api& a, ue::Reflection& r) : api(a), reflection(r) {}
    void remove(Tag& tag)
    {
        if (auto* widget = api.weak_get(&tag.widget))
        {
            Call call(reflection, L"/Script/UMG.Widget:RemoveFromParent");
            call.run(widget);
        }
        tag = {};
    }
    void visible(Tag& tag, bool show)
    {
        if (auto* widget = api.weak_get(&tag.widget))
        {
            Call visibility(reflection, L"/Script/UMG.Widget:SetVisibility");
            visibility.set(L"InVisibility", static_cast<unsigned char>(show ? 3 : 1)).run(widget);
        }
    }
    void create(Tag& tag, ue::Object* controller)
    {
        remove(tag);
        auto* type = api.weak_get(&widget_class);
        if (!type)
        {
            type = api.load(L"/Game/Mods/AHGhostCoopNavigation/WBP_GhostTag.WBP_GhostTag_C");
            widget_class = api.weak(type);
        }
        Call create(reflection, L"/Script/UMG.WidgetBlueprintLibrary:Create");
        create.set(L"WorldContextObject", controller)
            .set(L"WidgetType", type)
            .set(L"OwningPlayer", controller)
            .run(api.require(L"/Script/UMG.Default__WidgetBlueprintLibrary"));
        auto* widget = create.get<ue::Object*>();
        if (!widget)
            throw std::runtime_error("Ghost tag widget creation failed");
        tag.widget = api.weak(widget);
        tag.text = api.weak(api.object_property(widget, L"NameText"));
        tag.distance = api.weak(api.object_property(widget, L"DistanceText"));
        if (!api.weak_get(&tag.text) || !api.weak_get(&tag.distance))
            throw std::runtime_error("Ghost tag text bindings missing");
        Call add(reflection, L"/Script/UMG.UserWidget:AddToViewport");
        add.set(L"ZOrder", 100).run(widget);
        Call size(reflection, L"/Script/UMG.UserWidget:SetDesiredSizeInViewport");
        size.set(L"Size", Vec2{300, 90}).run(widget);
        Call align(reflection, L"/Script/UMG.UserWidget:SetAlignmentInViewport");
        align.set(L"Alignment", Vec2{0.5f, 1}).run(widget);
        visible(tag, false);
    }
    void screen(Tag& tag, ue::Object* controller, Vec2 point, const std::string& name,
                float distance)
    {
        if (!api.weak_get(&tag.widget))
            create(tag, controller);
        if (tag.caption != name)
        {
            text(api.weak_get(&tag.text), name);
            tag.caption = name;
        }
        std::string meters =
            std::to_string(static_cast<int>(std::min(distance / 100.0f, 1e7f))) + " m";
        if (tag.distance_caption != meters)
        {
            text(api.weak_get(&tag.distance), meters);
            tag.distance_caption = meters;
        }
        Call position(reflection, L"/Script/UMG.UserWidget:SetPositionInViewport");
        position.set(L"Position", point)
            .set(L"bRemoveDPIScale", true)
            .run(api.weak_get(&tag.widget));
        visible(tag, true);
    }
    void world(Tag& tag, ue::Object* controller, Vec3 point, const std::string& name,
               float distance)
    {
        Call project(reflection, L"/Script/Engine.GameplayStatics:ProjectWorldToScreen");
        project.set(L"Player", controller)
            .set(L"WorldPosition", point)
            .set(L"bPlayerViewportRelative", false)
            .run(api.require(L"/Script/Engine.Default__GameplayStatics"));
        if (!project.get<bool>())
        {
            visible(tag, false);
            return;
        }
        Vec2 position = project.get<Vec2>(L"ScreenPosition");
        Call viewport(reflection, L"/Script/Engine.PlayerController:GetViewportSize");
        viewport.run(controller);
        const int width = viewport.get<int>(L"SizeX"), height = viewport.get<int>(L"SizeY");
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || position.x < 0 ||
            position.y < 0 || position.x > width || position.y > height)
        {
            visible(tag, false);
            return;
        }
        screen(tag, controller, position, name, distance);
    }
};
} // namespace ghost
