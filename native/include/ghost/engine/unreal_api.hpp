#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <utility>
#include "ghost/engine/windows.hpp"
#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "ghost/engine/object_lifetime.hpp"

namespace ue
{
inline std::string utf8(const std::wstring& value);
struct Object;
struct Property;
struct Name
{
    std::uint32_t index{}, number{};
};
struct Rotation
{
    float pitch{}, yaw{}, roll{};
};
struct alignas(16) Transform
{
    float rotation[4]{0, 0, 0, 1};
    float translation[4]{};
    float scale[4]{1, 1, 1, 0};
};
struct String
{
    wchar_t* data{};
    std::int32_t count{}, capacity{};
};
static_assert(sizeof(String) == 16 && sizeof(Rotation) == 12);
static_assert(sizeof(Transform) == 48 && alignof(Transform) == 16);

class Api : private DeleteListener
{
    HMODULE module{};
    ObjectLifetime lifetime;
    std::map<std::wstring, Weak, std::less<>> objects;
    std::atomic_bool listening{};
    void (*add_listener)(DeleteListener*){};
    void (*remove_listener)(DeleteListener*){};
    void NotifyUObjectDeleted(const Object* object, int) override
    {
        lifetime.forget(object);
    }
    void OnUObjectArrayShutdown() override
    {
        detach();
    }
    template <class T> void bind(T& target, const char* name)
    {
        target = reinterpret_cast<T>(GetProcAddress(module, name));
        if (!target)
            throw std::runtime_error(std::string("UE4SS export missing: ") + name);
    }

public:
    std::atomic_bool uncached_lookup{};
    std::uint64_t slow_lookups{}; // Game-thread counter, copied to diagnostics once per window.
    Object* (*find)(Object*, Object*, const wchar_t*, bool){};
    void (*name_ctor)(Name*, const wchar_t*, int, void*){};
    Object* (*get_world)(const Object*){};
    void (*process_event)(Object*, Object*, void*){};
    Property* (*get_property)(Object*, const wchar_t*){};
    void* (*property_value)(const Property*, Object*, int){};
    Property*& (*child_properties)(Object*){};
    Property*& (*next_property)(Property*){};
    Name& (*property_name)(Property*){};
    int& (*offset)(Property*){};
    int& (*element_size)(Property*){};
    unsigned short& (*parms_size)(Object*){};
    void (*destroy_value)(Property*, void*){};
    Object* (*spawn)(Object*, Object*, const void*, const Rotation*){};
    void (*asset_ctor)(void*){};
    int (*asset_size)(){};
    void (*asset_path)(void*, Name){};
    Object* (*get_asset)(void*){};
    void (*root)(Object*){};
    bool (*has_flags)(Object*, int){};
    Object* (*outermost)(Object*){};
    std::wstring* (*get_name_into)(const Object*, void*){};

    void init()
    {
        module = GetModuleHandleW(L"UE4SS.dll");
        if (!module)
            throw std::runtime_error("UE4SS.dll is not loaded");
        bind(find, "?StaticFindObject_InternalSlow@UObjectGlobals@Unreal@RC@@YAPEAVUObject@23@"
                   "PEAVUClass@23@PEAV423@PEB_W_N@Z");
        bind(name_ctor, "??0FName@Unreal@RC@@QEAA@PEB_WW4EFindName@12@PEAX@Z");
        bind(add_listener, "?AddUObjectDeleteListener@UObjectArray@Unreal@RC@@"
                           "SAXPEAVFUObjectDeleteListener@23@@Z");
        bind(remove_listener, "?RemoveUObjectDeleteListener@UObjectArray@Unreal@RC@@"
                              "SAXPEAVFUObjectDeleteListener@23@@Z");
        bind(get_world, "?GetWorld@UObject@Unreal@RC@@QEBAPEAVUWorld@23@XZ");
        bind(process_event, "?ProcessEvent@UObject@Unreal@RC@@QEAAXPEAVUFunction@23@PEAX@Z");
        bind(get_property,
             "?GetPropertyByNameInChain@UObject@Unreal@RC@@QEAAPEAVFProperty@23@PEB_W@Z");
        bind(property_value, "?ContainerUObjectPtrToValuePtrInternal@FProperty@Unreal@RC@@"
                             "AEBAPEAXPEAVUObject@23@H@Z");
        bind(child_properties, "?GetChildProperties@UStruct@Unreal@RC@@QEAAAEAPEAVFField@23@XZ");
        bind(next_property, "?GetNext@FField@Unreal@RC@@AEAAAEAPEAV123@XZ");
        bind(property_name, "?GetNamePrivate@FField@Unreal@RC@@AEAAAEAVFName@23@XZ");
        bind(offset, "?GetOffset_Internal@FProperty@Unreal@RC@@QEAAAEAHXZ");
        bind(element_size, "?GetElementSize@FProperty@Unreal@RC@@QEAAAEAHXZ");
        bind(parms_size, "?GetParmsSize@UFunction@Unreal@RC@@QEAAAEAGXZ");
        bind(destroy_value, "?DestroyValue@FProperty@Unreal@RC@@QEAAXPEAX@Z");
        bind(spawn, "?SpawnActor@UWorld@Unreal@RC@@QEAAPEAVAActor@23@PEAVUClass@23@PEBUFVector@23@"
                    "PEBUFRotator@23@@Z");
        bind(asset_ctor, "??0FAssetData@Unreal@RC@@QEAA@XZ");
        bind(asset_size, "?StaticSize@FAssetData@Unreal@RC@@SAHXZ");
        bind(asset_path, "?SetObjectPath@FAssetData@Unreal@RC@@QEAAXVFName@23@@Z");
        bind(get_asset,
             "?GetAsset@UAssetRegistryHelpers@Unreal@RC@@SAPEAVUObject@23@AEAUFAssetData@23@@Z");
        bind(root, "?SetRootSet@UObject@Unreal@RC@@QEAAXXZ");
        bind(has_flags,
             "?HasAnyInternalFlags@UObject@Unreal@RC@@QEAA_NW4EInternalObjectFlags@23@@Z");
        bind(outermost, "?GetOutermost@UObject@Unreal@RC@@QEAAPEAV123@XZ");
        bind(get_name_into, "?GetName@UObject@Unreal@RC@@QEBA?AV?$basic_string@_WU?$char_traits@_W@"
                            "std@@V?$allocator@_W@2@@std@@XZ");
        add_listener(this);
        listening = true;
    }
    void detach()
    {
        if (listening.exchange(false))
            remove_listener(this);
        lifetime.clear();
    }
    ~Api() override
    {
        detach();
    }
    std::wstring get_name(const Object* object) const
    {
        // MSVC member functions pass this before the hidden nontrivial return buffer.
        alignas(std::wstring) unsigned char storage[sizeof(std::wstring)];
        auto* result = get_name_into(object, storage);
        std::wstring value = std::move(*result);
        std::destroy_at(result);
        return value;
    }
    Weak weak(Object* object)
    {
        return lifetime.track(object);
    }
    Object* weak_get(const Weak* handle) const
    {
        auto* object = ObjectLifetime::get(*handle);
        // These reads occur only on the game thread, outside the world's GC pass.
        return object && !has_flags(object, (1 << 28) | (1 << 29)) ? object : nullptr;
    }
    Object* object_property(Object* object, const wchar_t* name) const
    {
        if (!object)
            return nullptr;
        auto* p = get_property(object, name);
        if (!p || element_size(p) != sizeof(Object*))
            return nullptr;
        auto* storage = property_value(p, object, 0);
        return storage ? *static_cast<Object**>(storage) : nullptr;
    }
    template <class T> T read_property(Object* object, const wchar_t* name) const
    {
        if (!object)
            throw std::runtime_error("property owner expired");
        auto* property = get_property(object, name);
        if (!property || element_size(property) != sizeof(T))
            throw std::runtime_error("property missing or size mismatch: " + utf8(name));
        auto* storage = property_value(property, object, 0);
        if (!storage)
            throw std::runtime_error("property storage unavailable");
        T value;
        std::memcpy(&value, storage, sizeof(T));
        return value;
    }
    Object* require(const wchar_t* path)
    {
        if (!uncached_lookup.load(std::memory_order_relaxed))
        {
            auto it = objects.find(path);
            if (it != objects.end())
                if (auto* object = weak_get(&it->second))
                    return object;
        }
        ++slow_lookups;
        auto* object = find(nullptr, nullptr, path, false);
        if (!object)
            throw std::runtime_error("Unreal object/function not found: " + utf8(path));
        objects[path] = weak(object);
        return object;
    }
    Object* load(const wchar_t* path) const
    {
        const int size = asset_size();
        if (size <= 0 || size > 4096)
            throw std::runtime_error("unsupported FAssetData size");
        // This UE4SS build's constructor writes 0x78 bytes despite StaticSize returning 0x60.
        std::vector<std::uint64_t> storage((std::max(size, 0x78) + 7) / 8);
        asset_ctor(storage.data());
        Name name;
        name_ctor(&name, path, 1, nullptr);
        asset_path(storage.data(), name);
        Object* asset = get_asset(storage.data());
        if (!asset)
            throw std::runtime_error("Asset unavailable: " + utf8(path));
        // Only shared package assets are rooted, never actors, worlds or player pawns.
        root(asset);
        return asset;
    }
};

struct Field
{
    Property* property{};
    int offset{}, size{};
};
struct Function
{
    Weak object;
    unsigned short size{};
    std::map<std::wstring, Field> fields;
    std::wstring path;
};

class Reflection
{
    Api& api;
    std::map<std::wstring, Function> functions;

public:
    explicit Reflection(Api& api_) : api(api_) {}
    Function& resolve(const wchar_t* path)
    {
        auto& fn = functions[path];
        if (!api.weak_get(&fn.object))
        {
            Object* object = api.require(path);
            fn = {api.weak(object), api.parms_size(object), {}, path};
            if (fn.size > 4096)
                throw std::runtime_error("UFunction parameter size exceeds bound");
        }
        return fn;
    }
    Field field(Function& fn, const wchar_t* name)
    {
        auto it = fn.fields.find(name);
        if (it != fn.fields.end())
            return it->second;
        Name fname;
        api.name_ctor(&fname, name, 1, nullptr);
        Property* property = nullptr;
        std::string available;
        auto* object = api.weak_get(&fn.object);
        if (!object)
            throw std::runtime_error("UFunction expired: " + utf8(fn.path));
        // Walk the function's own fields, without UE4SS's shared property lookup cache.
        auto* candidate = api.child_properties(object);
        for (int count = 0; candidate && count < 128;
             ++count, candidate = api.next_property(candidate))
        {
            const Name id = api.property_name(candidate);
            available += std::to_string(id.index) + ",";
            if (id.index == fname.index && id.number == fname.number)
            {
                property = candidate;
                break;
            }
        }
        if (!property)
            throw std::runtime_error(
                "UFunction parameter missing: " + utf8(fn.path + L" : " + name) +
                "; object=" + utf8(api.get_name(object)) + "; size=" + std::to_string(fn.size) +
                "; wanted=" + std::to_string(fname.index) + "; fields=" + available);
        Field f{property, api.offset(property), api.element_size(property)};
        if (f.offset < 0 || f.size <= 0 || f.offset + f.size > fn.size)
            throw std::runtime_error("UFunction parameter layout invalid");
        fn.fields[name] = f;
        return f;
    }
    class Call
    {
        Reflection& reflection;
        Function& function;
        alignas(16) std::array<unsigned char, 4096> buffer{};

    public:
        Call(Reflection& r, const wchar_t* path) : reflection(r), function(r.resolve(path)) {}
        template <class T> Call& set(const wchar_t* name, const T& value)
        {
            auto f = reflection.field(function, name);
            if (f.size != sizeof(T))
                throw std::runtime_error("UFunction input size mismatch");
            std::memcpy(buffer.data() + f.offset, &value, sizeof(T));
            return *this;
        }
        template <class T> T get(const wchar_t* name = L"ReturnValue")
        {
            auto f = reflection.field(function, name);
            if (f.size != sizeof(T))
                throw std::runtime_error("UFunction output size mismatch");
            T result;
            std::memcpy(&result, buffer.data() + f.offset, sizeof(T));
            return result;
        }
        void copy(const wchar_t* destination, Call& source, const wchar_t* name)
        {
            auto a = reflection.field(function, destination);
            auto b = source.reflection.field(source.function, name);
            if (a.size != b.size)
                throw std::runtime_error("UFunction copy size mismatch");
            std::memcpy(buffer.data() + a.offset, source.buffer.data() + b.offset, a.size);
        }
        void destroy(const wchar_t* name)
        {
            auto f = reflection.field(function, name);
            reflection.api.destroy_value(f.property, buffer.data() + f.offset);
            std::memset(buffer.data() + f.offset, 0, f.size);
        }
        void run(Object* target)
        {
            Object* function_object = reflection.api.weak_get(&function.object);
            if (!target || !function_object)
                throw std::runtime_error("call target expired");
            reflection.api.process_event(target, function_object, buffer.data());
        }
    };
};
inline std::string utf8(const std::wstring& value)
{
    if (value.empty())
        return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                   nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                        size, nullptr, nullptr);
    return result;
}
inline std::wstring wide(const std::string& value)
{
    if (value.empty())
        return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                   static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
        throw std::runtime_error("invalid UTF-8 name");
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}
} // namespace ue
