#pragma once
#include "ghost/engine/windows.hpp"
#include "ghost/platform/steam_client.hpp"
#include "ghost/platform/steam_client_route.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <string>

namespace ghost::platform
{
// This SDK instance belongs to the mod. Never initialize, shut down, or obtain
// interfaces from the game's existing steam_api64.dll (which may use a different client).
class SteamOverlay
{
    HMODULE module{}, client{};
    std::unique_ptr<SteamClientRoute> client_route;
    bool initialized{};
    bool owns_overlay_game_id{};
    std::wstring previous_overlay_game_id;
    std::uint32_t app_id{};
    std::mutex mutex;
    void (*shutdown)(){};
    void (*frame)(int){};
    struct Callback
    {
        int user{}, event{};
        void* data{};
        int size{};
    };
    bool (*next)(int, Callback*){};
    void (*free_callback)(int){};
    bool (*enabled)(void*){};
    void (*invite)(void*, std::uint64_t){};
    void* friends{};
    void* utils{};
    int pipe{};

    struct AppEnvironment
    {
        static constexpr std::array<const wchar_t*, 8> keys{
            L"SteamAppId", L"SteamGameId", L"SteamPath", L"SteamAppUser", L"SteamUser",
            L"SteamClientLaunch", L"SteamEnv", L"SteamOverlayGameId"};
        std::array<std::wstring, keys.size()> values;
        bool keep_overlay_game_id{};
        static std::wstring read(const wchar_t* key)
        {
            const DWORD length = GetEnvironmentVariableW(key, nullptr, 0);
            if (!length)
                return {};
            std::wstring value(length, L'\0');
            const DWORD count = GetEnvironmentVariableW(key, value.data(), length);
            value.resize(count < length ? count : 0);
            return value;
        }
        explicit AppEnvironment(const std::filesystem::path& directory)
        {
            for (std::size_t i = 0; i < keys.size(); ++i)
            {
                values[i] = read(keys[i]);
                SetEnvironmentVariableW(keys[i], nullptr);
            }
            SetEnvironmentVariableW(L"SteamAppId", L"480");
            SetEnvironmentVariableW(L"SteamGameId", L"480");
            SetEnvironmentVariableW(L"SteamPath", directory.c_str());
        }
        ~AppEnvironment()
        {
            for (std::size_t i = 0; i < keys.size(); ++i)
            {
                // The SDK publishes this value for the asynchronous Overlay UI.
                // Restoring the loader's ID here changes that UI's context after
                // successful initialization. Other game routing stays unchanged.
                if (keep_overlay_game_id && i == keys.size() - 1)
                    continue;
                SetEnvironmentVariableW(keys[i], values[i].empty() ? nullptr : values[i].c_str());
            }
        }
    };
    template <class Function> Function symbol(const char* name)
    {
        auto address = GetProcAddress(module, name);
        if (!address)
            throw std::runtime_error(std::string("Mod Steamworks runtime missing export: ") + name);
        return reinterpret_cast<Function>(address);
    }
    void* interface(const char* current, const char* previous = nullptr)
    {
        using Accessor = void* (*)();
        auto address = GetProcAddress(module, current);
        if (!address && previous)
            address = GetProcAddress(module, previous);
        void* value = address ? reinterpret_cast<Accessor>(address)() : nullptr;
        if (!value)
            throw std::runtime_error(std::string("Mod Steamworks interface unavailable: ") + current);
        return value;
    }
    static void load_renderer(const std::filesystem::path& directory)
    {
        // Its graphics hooks must stay loaded for the process lifetime.
        const auto renderer = directory / L"gameoverlayrenderer64.dll";
        HMODULE existing{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, renderer.c_str(), &existing) &&
            !LoadLibraryExW(renderer.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
            throw std::runtime_error("Could not load the running Steam client's Overlay renderer");
    }
    void close_unlocked()
    {
        if (initialized && shutdown)
            shutdown();
        if (owns_overlay_game_id && AppEnvironment::read(L"SteamOverlayGameId") == L"480")
            SetEnvironmentVariableW(L"SteamOverlayGameId",
                previous_overlay_game_id.empty() ? nullptr : previous_overlay_game_id.c_str());
        owns_overlay_game_id = false;
        initialized = false;
        app_id = 0;
        client_route.reset();
        if (module)
            FreeLibrary(module);
        module = nullptr;
        if (client)
            FreeLibrary(client);
        client = nullptr;
        friends = utils = nullptr;
    }
    void initialize_unlocked(const std::filesystem::path& runtime)
    {
        if (initialized)
            return;
        if (!std::filesystem::is_regular_file(runtime))
            throw std::runtime_error("Install the mod's official Steamworks runtime for Steam Overlay");
        const auto original = GetModuleHandleW(L"steam_api64.dll");
        module = LoadLibraryExW(runtime.c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module)
        {
            close_unlocked();
            throw std::runtime_error("Could not load the mod's independent Steamworks runtime");
        }
        if (module == original)
        {
            FreeLibrary(module);
            module = nullptr;
            close_unlocked();
            throw std::runtime_error("Overlay runtime resolved to the game's SDK; independent mod runtime required");
        }
        try
        {
            shutdown = symbol<void (*)()>("SteamAPI_Shutdown");
            const auto init = symbol<int (*)(void*)>("SteamAPI_InitFlat");
            const auto directory = running_steam_directory();
            AppEnvironment environment(directory);
            client = LoadLibraryExW((directory / L"steamclient64.dll").c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!client)
                throw std::runtime_error("Could not load the running Steam client's steamclient64.dll");
            client_route = std::make_unique<SteamClientRoute>(module, client,
                (directory / L"steamclient64.dll").wstring());
            load_renderer(directory);
            char error[1024]{};
            if (init(error) != 0)
                throw std::runtime_error(std::string("Steam Overlay initialization failed: ") + error);
            initialized = true;
            utils = interface("SteamAPI_SteamUtils_v011", "SteamAPI_SteamUtils_v010");
            app_id = symbol<std::uint32_t (*)(void*)>("SteamAPI_ISteamUtils_GetAppID")(utils);
            if (app_id != 480)
                throw std::runtime_error("Mod Overlay initialized under AppID " + std::to_string(app_id) +
                                         "; expected Spacewar 480");
            auto user = interface("SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022");
            if (!symbol<bool (*)(void*)>("SteamAPI_ISteamUser_BLoggedOn")(user))
                throw std::runtime_error("Sign in to the real Steam client before opening invitations");
            friends = interface("SteamAPI_SteamFriends_v018", "SteamAPI_SteamFriends_v017");
            enabled = symbol<bool (*)(void*)>("SteamAPI_ISteamUtils_IsOverlayEnabled");
            invite = symbol<void (*)(void*, std::uint64_t)>("SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog");
            symbol<void (*)()>("SteamAPI_ManualDispatch_Init")();
            pipe = symbol<int (*)()>("SteamAPI_GetHSteamPipe")();
            frame = symbol<void (*)(int)>("SteamAPI_ManualDispatch_RunFrame");
            next = symbol<bool (*)(int, Callback*)>("SteamAPI_ManualDispatch_GetNextCallback");
            free_callback = symbol<void (*)(int)>("SteamAPI_ManualDispatch_FreeLastCallback");
            previous_overlay_game_id = environment.values.back();
            SetEnvironmentVariableW(L"SteamOverlayGameId", L"480");
            environment.keep_overlay_game_id = true;
            owns_overlay_game_id = true;
        }
        catch (...)
        {
            close_unlocked();
            throw;
        }
    }

public:
    ~SteamOverlay() { close_unlocked(); }
    // Prime only the graphics hooks before Unreal starts. Loading a second Steam
    // API DLL here could break the game's delayed SDK imports, so defer the SDK.
    void prepare_renderer()
    {
        std::scoped_lock lock(mutex);
        const auto directory = running_steam_directory();
        AppEnvironment environment(directory);
        load_renderer(directory);
    }
    // Public initialization also supports a standalone SDK smoke check without opening UI.
    void initialize(const std::filesystem::path& runtime)
    {
        std::scoped_lock lock(mutex);
        initialize_unlocked(runtime);
    }
    bool attached()
    {
        std::scoped_lock lock(mutex);
        return initialized && enabled(utils);
    }
    std::uint32_t application_id()
    {
        std::scoped_lock lock(mutex);
        return app_id;
    }
    void open(const std::filesystem::path& runtime, std::uint64_t lobby)
    {
        if (!lobby)
            throw std::runtime_error("Create or join a Steam lobby first");
        std::scoped_lock lock(mutex);
        initialize_unlocked(runtime);
        if (!enabled(utils))
            throw std::runtime_error("Real Steam Overlay is not attached to the game; check Steam Overlay settings");
        invite(friends, lobby);
    }
    std::uint64_t poll_invitation()
    {
        std::scoped_lock lock(mutex);
        if (!initialized)
            return 0;
        frame(pipe);
        Callback callback{};
        std::uint64_t lobby{};
        for (int i = 0; i < 128 && next(pipe, &callback); ++i)
        {
            // GameLobbyJoinRequested_t contains lobby ID and inviter SteamID.
            if (callback.event == 333 && callback.size == 16 && callback.data)
                std::memcpy(&lobby, callback.data, sizeof(lobby));
            free_callback(pipe);
        }
        return lobby;
    }
};
} // namespace ghost::platform
