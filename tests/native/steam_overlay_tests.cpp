#include "ghost/platform/steam_overlay.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

static void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
int main()
{
    try
    {
        ghost::platform::SteamOverlay overlay;
        check(overlay.application_id() == 0 && !overlay.attached(),
              "uninitialized Overlay must report no context or renderer");
        check(overlay.poll_invitation() == 0, "uninitialized Overlay must not consume callbacks");
        bool rejected{};
        try { overlay.open(L"missing.dll", 0); }
        catch (const std::runtime_error& error)
        { rejected = std::string(error.what()).find("lobby first") != std::string::npos; }
        check(rejected, "no lobby must be rejected without initializing any SDK");
        rejected = false;
        try { overlay.initialize(L"missing-steamworks-runtime.dll"); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "missing independent SDK must be rejected");
        wchar_t system[MAX_PATH]{};
        GetSystemDirectoryW(system, MAX_PATH);
        const auto foreign = std::filesystem::path(system) / L"kernel32.dll";
        const DWORD before = GetEnvironmentVariableW(L"SteamAppId", nullptr, 0);
        std::wstring original(before, L'\0');
        if (before)
            original.resize(GetEnvironmentVariableW(L"SteamAppId", original.data(), before));
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            rejected = false;
            try { overlay.initialize(foreign); }
            catch (const std::runtime_error&) { rejected = true; }
            check(rejected, "non-SDK library must be rejected and may be retried");
            const DWORD after = GetEnvironmentVariableW(L"SteamAppId", nullptr, 0);
            std::wstring restored(after, L'\0');
            if (after)
                restored.resize(GetEnvironmentVariableW(L"SteamAppId", restored.data(), after));
            check(original == restored && before == after,
                  "failed SDK setup must restore the game's Steam environment");
        }
        std::cout << "Independent Steam Overlay ownership and failure cleanup tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
