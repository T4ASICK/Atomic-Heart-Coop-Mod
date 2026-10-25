#pragma once
#include "ghost/engine/windows.hpp"
#include <filesystem>
#include <string>

// Snapshot exchange with the Python bridge. Reads are bounded; writes replace complete files.
namespace ghost::platform
{
inline bool read_shared(const std::filesystem::path& path, std::string& text)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    char buffer[8193];
    DWORD count = 0;
    bool ok = ReadFile(file, buffer, sizeof(buffer), &count, nullptr) && count < sizeof(buffer);
    CloseHandle(file);
    if (ok)
        text.assign(buffer, count);
    return ok;
}
inline bool write_atomic(const std::filesystem::path& path, const std::string& text)
{
    auto temp = path;
    temp += L".native-tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
              written == text.size();
    CloseHandle(file);
    return ok && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

} // namespace ghost::platform
