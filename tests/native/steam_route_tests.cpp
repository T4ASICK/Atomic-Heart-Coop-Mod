#include "ghost/platform/steam_client_route.hpp"
#include <filesystem>
#include <iostream>

static void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
int main()
{
    try
    {
        const auto game_path = std::filesystem::absolute(L"route_game/steamclient64.dll");
        const auto real_path = std::filesystem::absolute(L"route_real/steamclient64.dll");
        const auto game = LoadLibraryW(game_path.c_str());
        const auto real = LoadLibraryW(real_path.c_str());
        const auto sdk = LoadLibraryW(std::filesystem::absolute(L"steam_route_fixture.dll").c_str());
        check(game && real && sdk && game != real, "two same-name client fixtures must load separately");
        const auto probe = reinterpret_cast<HMODULE (*)()>(GetProcAddress(sdk, "probe_client_a"));
        check(probe && probe() == game, "unrouted SDK must see the game client loaded first");
        {
            ghost::platform::SteamClientRoute route(sdk, real, real_path.wstring());
            for (const auto name : {"probe_client_a", "probe_client_w", "probe_client_ex_w"})
            {
                const auto call = reinterpret_cast<HMODULE (*)()>(GetProcAddress(sdk, name));
                check(call && call() == real, "SDK handle lookups must select only the real client");
            }
            for (const auto name : {"probe_load_a", "probe_load_w"})
            {
                const auto call = reinterpret_cast<HMODULE (*)()>(GetProcAddress(sdk, name));
                const auto loaded = call ? call() : nullptr;
                check(loaded == real, "SDK loading must select the real client");
                FreeLibrary(loaded);
            }
            check(GetModuleHandleW(L"steamclient64.dll") == game,
                  "routing must not change imports in the game or other modules");
            const auto absolute_load = reinterpret_cast<HMODULE (*)(LPCWSTR)>(GetProcAddress(sdk, "probe_load_path_w"));
            check(absolute_load != nullptr, "absolute client load probe missing");
            const auto absolute_client = absolute_load(game_path.c_str());
            check(absolute_client == real, "a game-local client path from the registry must route to the real client");
            FreeLibrary(absolute_client);
            const auto unrelated = reinterpret_cast<HMODULE (*)()>(GetProcAddress(sdk, "probe_unrelated"));
            check(unrelated && unrelated() == GetModuleHandleW(L"kernel32.dll"),
                  "unrelated module lookups must be preserved");
            bool rejected{};
            try { ghost::platform::SteamClientRoute duplicate(sdk, real, real_path.wstring()); }
            catch (const std::runtime_error&) { rejected = true; }
            check(rejected && probe() == real, "duplicate setup must preserve existing routing");
        }
        check(probe() == game, "destruction must restore original SDK imports");
        {
            ghost::platform::SteamClientRoute retry(sdk, real, real_path.wstring());
            check(probe() == real, "routing must be reusable after cleanup");
        }
        FreeLibrary(sdk);
        FreeLibrary(real);
        FreeLibrary(game);
        std::cout << "Scoped Steam client routing and import restoration tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
