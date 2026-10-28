#include "ghost/platform/steam_overlay.hpp"
#include <iostream>

static std::wstring read_environment(const wchar_t* name)
{
    wchar_t value[512]{};
    GetEnvironmentVariableW(name, value, 512);
    return value;
}

// Optional live check: official mod SDK, then an existing game SDK to keep loaded.
// Never requests an Overlay dialog or sends an invitation.
int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
        return 2;
    HMODULE original{};
    HMODULE game_client{};
    bool original_initialized{};
    void (*original_shutdown)(){};
    std::uint32_t (*original_app)(void*){};
    void* original_utils{};
    std::uint32_t app_before{};
    try
    {
        if (std::wstring(argv[1]) == L"--early-sdk")
        {
            if (argc < 3) return 2;
            const auto before = GetModuleHandleW(L"steam_api64.dll");
            ghost::platform::SteamOverlay overlay;
            overlay.initialize(std::filesystem::absolute(argv[2]));
            if (GetModuleHandleW(L"steam_api64.dll") != before || overlay.application_id() != 480)
                throw std::runtime_error("Early mod SDK must not occupy the game's steam_api64.dll name");
            if (argc >= 4)
            {
                const auto game_sdk = LoadLibraryExW(argv[3], nullptr,
                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if (!game_sdk || GetModuleHandleW(L"steam_api64.dll") != game_sdk ||
                    !GetProcAddress(game_sdk, "SteamAPI_SteamUser_v021"))
                    throw std::runtime_error("Game SDK delayed imports did not retain the original module");
                FreeLibrary(game_sdk);
            }
            std::cout << "Early AppID 480 SDK initialized with a distinct DLL name; game SDK imports preserved\n";
            return 0;
        }
        if (std::wstring(argv[1]) == L"--prime-renderer-only")
        {
            const auto before = GetModuleHandleW(L"steam_api64.dll");
            ghost::platform::SteamOverlay overlay;
            overlay.prepare_renderer();
            if (GetModuleHandleW(L"steam_api64.dll") != before || overlay.application_id())
                throw std::runtime_error("Early renderer preparation must not load or initialize a Steam SDK");
            std::cout << "Valve renderer primed without loading or initializing a Steam API DLL\n";
            return 0;
        }
        if (argc >= 5)
        {
            game_client = LoadLibraryExW(argv[4], nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!game_client)
                throw std::runtime_error("Could not preload existing game client");
        }
        if (argc >= 3)
        {
            original = LoadLibraryExW(argv[2], nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!original)
                throw std::runtime_error("Could not load the existing SDK for coexistence check");
            if (argc >= 4 && std::wstring(argv[3]) == L"--initialize-game-sdk")
            {
                const auto init = reinterpret_cast<bool (*)()>(GetProcAddress(original, "SteamAPI_InitSafe"));
                original_shutdown = reinterpret_cast<void (*)()>(GetProcAddress(original, "SteamAPI_Shutdown"));
                const auto utils = reinterpret_cast<void* (*)()>(GetProcAddress(original, "SteamAPI_SteamUtils_v010"));
                original_app = reinterpret_cast<std::uint32_t (*)(void*)>(
                    GetProcAddress(original, "SteamAPI_ISteamUtils_GetAppID"));
                if (!init || !original_shutdown || !utils || !original_app)
                    throw std::runtime_error("Existing game SDK exports are incomplete");
                bool started{};
                if (game_client)
                {
                    // Reproduce the loader-managed client in this isolated test
                    // process without changing the machine's Steam registry.
                    SetEnvironmentVariableW(L"SteamAppId", L"668580");
                    SetEnvironmentVariableW(L"SteamGameId", L"668580");
                    ghost::platform::SteamClientRoute game_route(original, game_client, argv[4]);
                    started = init();
                }
                else started = init();
                if (!started)
                    throw std::runtime_error("Could not initialize existing game SDK for coexistence check");
                original_initialized = true;
                original_utils = utils();
                if (!original_utils)
                    throw std::runtime_error("Existing game SDK has no Utils interface");
                app_before = original_app(original_utils);
                std::cout << "Existing game context AppID: " << app_before << '\n';
            }
        }
        const auto overlay_id_before = read_environment(L"SteamOverlayGameId");
        {
            const auto game_id_before = read_environment(L"SteamGameId");
            const auto app_id_before = read_environment(L"SteamAppId");
            ghost::platform::SteamOverlay overlay;
            overlay.initialize(std::filesystem::absolute(argv[1]));
            (void)overlay.poll_invitation();
            std::cout << "Mod UI SDK initialized under AppID " << overlay.application_id() << '\n';
            if (overlay.application_id() != 480)
                throw std::runtime_error("Mod context must use Spacewar 480");
            if (read_environment(L"SteamOverlayGameId") != L"480" ||
                read_environment(L"SteamGameId") != game_id_before ||
                read_environment(L"SteamAppId") != app_id_before)
                throw std::runtime_error("Overlay must retain its SDK identity without changing the game's routing");
            std::cout << "Overlay environment retains 480; game AppID and GameID preserved\n";
            std::cout << "Real Overlay renderer: " << (overlay.attached() ? "attached" : "not attached (no game rendering)") << '\n';
        }
        if (read_environment(L"SteamOverlayGameId") != overlay_id_before)
            throw std::runtime_error("Mod shutdown must restore the previous Overlay identity");
        std::cout << "Previous Overlay identity restored after mod SDK shutdown\n";
        if (original && GetModuleHandleW(L"steam_api64.dll") != original)
            throw std::runtime_error("Original SDK was not preserved after mod context shutdown");
        if (original)
        {
            if (original_initialized)
            {
                if (original_app(original_utils) != app_before)
                    throw std::runtime_error("Existing game context AppID changed");
                original_shutdown();
                original_initialized = false;
                std::cout << "Existing initialized game context retained its AppID\n";
            }
            FreeLibrary(original);
            std::cout << "Existing game SDK preserved through independent SDK initialization and shutdown\n";
        }
        if (game_client) FreeLibrary(game_client);
        return 0;
    }
    catch (const std::exception& error)
    {
        if (original_initialized) original_shutdown();
        if (original) FreeLibrary(original);
        if (game_client) FreeLibrary(game_client);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
