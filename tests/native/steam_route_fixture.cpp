#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" __declspec(dllexport) bool SteamAPI_InitSafe() { return true; }
extern "C" __declspec(dllexport) HMODULE probe_client_a() { return GetModuleHandleA("steamclient64.dll"); }
extern "C" __declspec(dllexport) HMODULE probe_client_w() { return GetModuleHandleW(L"steamclient64.dll"); }
extern "C" __declspec(dllexport) HMODULE probe_client_ex_w()
{
    HMODULE result{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"steamclient64.dll", &result);
    return result;
}
extern "C" __declspec(dllexport) HMODULE probe_load_a() { return LoadLibraryExA("steamclient64.dll", nullptr, 0); }
extern "C" __declspec(dllexport) HMODULE probe_load_w() { return LoadLibraryExW(L"steamclient64.dll", nullptr, 0); }
extern "C" __declspec(dllexport) HMODULE probe_load_path_w(LPCWSTR path) { return LoadLibraryExW(path, nullptr, 0); }
extern "C" __declspec(dllexport) HMODULE probe_unrelated() { return GetModuleHandleW(L"kernel32.dll"); }
