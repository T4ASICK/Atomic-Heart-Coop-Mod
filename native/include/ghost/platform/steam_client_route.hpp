#pragma once
#include "ghost/engine/windows.hpp"
#include <atomic>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ghost::platform
{
// Scope client selection to the mod SDK's import table. Windows can otherwise
// resolve steamclient64.dll to a game-local client already loaded in this process.
// Neither the game's imports nor Steam's registry are changed.
class SteamClientRoute
{
    struct Patch { void** slot; void* original; void* replacement; };
    std::vector<Patch> patches;
    HMODULE client;
    std::wstring path;
    static inline std::atomic<SteamClientRoute*> active{};

    static bool matches(const wchar_t* name)
    {
        if (!name) return false;
        const auto back = std::wcsrchr(name, L'\\');
        const auto forward = std::wcsrchr(name, L'/');
        if (back) name = back + 1;
        if (forward && (!back || forward > back)) name = forward + 1;
        return _wcsicmp(name, L"steamclient64.dll") == 0;
    }
    static bool matches(const char* name)
    {
        if (!name) return false;
        const auto back = std::strrchr(name, '\\');
        const auto forward = std::strrchr(name, '/');
        if (back) name = back + 1;
        if (forward && (!back || forward > back)) name = forward + 1;
        return _stricmp(name, "steamclient64.dll") == 0;
    }
    static HMODULE WINAPI get_a(LPCSTR name)
    {
        const auto route = active.load();
        return route && matches(name) ? route->client : GetModuleHandleA(name);
    }
    static HMODULE WINAPI get_w(LPCWSTR name)
    {
        const auto route = active.load();
        return route && matches(name) ? route->client : GetModuleHandleW(name);
    }
    static BOOL WINAPI get_ex_w(DWORD flags, LPCWSTR name, HMODULE* result)
    {
        const auto route = active.load();
        if (route && !(flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) && matches(name))
            return GetModuleHandleExW(flags, route->path.c_str(), result);
        return GetModuleHandleExW(flags, name, result);
    }
    static HMODULE WINAPI load_a(LPCSTR name, HANDLE file, DWORD flags)
    {
        const auto route = active.load();
        return route && matches(name) ? LoadLibraryExW(route->path.c_str(), file, flags)
                                      : LoadLibraryExA(name, file, flags);
    }
    static HMODULE WINAPI load_w(LPCWSTR name, HANDLE file, DWORD flags)
    {
        const auto route = active.load();
        return LoadLibraryExW(route && matches(name) ? route->path.c_str() : name, file, flags);
    }
    static void* replacement(const char* name)
    {
        if (!std::strcmp(name, "GetModuleHandleA")) return reinterpret_cast<void*>(&get_a);
        if (!std::strcmp(name, "GetModuleHandleW")) return reinterpret_cast<void*>(&get_w);
        if (!std::strcmp(name, "GetModuleHandleExW")) return reinterpret_cast<void*>(&get_ex_w);
        if (!std::strcmp(name, "LoadLibraryExA")) return reinterpret_cast<void*>(&load_a);
        if (!std::strcmp(name, "LoadLibraryExW")) return reinterpret_cast<void*>(&load_w);
        return nullptr;
    }
    static bool exchange(void** slot, void* expected, void* value)
    {
        DWORD protection{};
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
        const auto original = InterlockedCompareExchangePointer(slot, value, expected);
        DWORD ignored{};
        VirtualProtect(slot, sizeof(void*), protection, &ignored);
        return original == expected;
    }
    void restore() noexcept
    {
        for (auto i = patches.rbegin(); i != patches.rend(); ++i)
            exchange(i->slot, i->replacement, i->original);
        patches.clear();
        active = nullptr;
    }

public:
    SteamClientRoute(HMODULE sdk, HMODULE real_client, std::wstring real_path)
        : client(real_client), path(std::move(real_path))
    {
        if (!sdk || !client || !GetProcAddress(sdk, "SteamAPI_InitSafe") || !matches(path.c_str()))
            throw std::runtime_error("Invalid mod Steam SDK or client for isolated routing");
        SteamClientRoute* expected{};
        if (!active.compare_exchange_strong(expected, this))
            throw std::runtime_error("Mod Steam client routing is already active");
        try
        {
            auto base = reinterpret_cast<unsigned char*>(sdk);
            auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
                nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                throw std::runtime_error("Mod Steam SDK is not a Windows x64 module");
            const auto imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            const auto size = nt->OptionalHeader.SizeOfImage;
            if (!imports.VirtualAddress || imports.VirtualAddress >= size)
                throw std::runtime_error("Mod Steam SDK has no import directory");
            auto entry = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);
            for (; entry->Name; ++entry)
            {
                if (!entry->OriginalFirstThunk || !entry->FirstThunk) continue;
                auto names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + entry->OriginalFirstThunk);
                auto addresses = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + entry->FirstThunk);
                for (; names->u1.AddressOfData; ++names, ++addresses)
                {
                    if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
                    if (names->u1.AddressOfData >= size)
                        throw std::runtime_error("Mod Steam SDK has an invalid import name");
                    const auto imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                    if (const auto value = replacement(imported->Name))
                    {
                        auto slot = reinterpret_cast<void**>(&addresses->u1.Function);
                        patches.push_back({slot, *slot, value});
                        if (!exchange(slot, patches.back().original, value))
                            throw std::runtime_error("Could not route the mod SDK to the real Steam client");
                    }
                }
            }
            if (patches.empty())
                throw std::runtime_error("Mod Steam SDK has no supported client-loading imports");
        }
        catch (...) { restore(); throw; }
    }
    ~SteamClientRoute() { restore(); }
    SteamClientRoute(const SteamClientRoute&) = delete;
    SteamClientRoute& operator=(const SteamClientRoute&) = delete;
};
} // namespace ghost::platform
