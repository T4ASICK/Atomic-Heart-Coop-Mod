#pragma once
#include "ghost/engine/windows.hpp"
#include <tlhelp32.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace ghost::platform
{
// A game loader may overwrite Steam's registry paths. Resolve the running client
// executable instead, without changing the game or the user's registry.
inline std::filesystem::path running_steam_directory()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Could not find the running Steam client");
    std::filesystem::path directory;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
        do
        {
            if (_wcsicmp(entry.szExeFile, L"steam.exe") != 0)
                continue;
            const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, entry.th32ProcessID);
            if (!process)
                continue;
            std::wstring buffer(32768, L'\0');
            DWORD length = static_cast<DWORD>(buffer.size());
            const bool read = QueryFullProcessImageNameW(process, 0, buffer.data(), &length) != 0;
            CloseHandle(process);
            if (read)
            {
                buffer.resize(length);
                const auto candidate = std::filesystem::path(buffer).parent_path();
                std::error_code error;
                if (std::filesystem::is_regular_file(candidate / L"steamclient64.dll", error))
                {
                    directory = candidate;
                    break;
                }
            }
        } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    if (directory.empty())
        throw std::runtime_error("Start the real Steam client and sign in before opening invitations");
    return directory;
}
} // namespace ghost::platform
