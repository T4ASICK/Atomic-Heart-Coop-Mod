#pragma once
#include "ghost/engine/windows.hpp"
#include <filesystem>
#include <string>
#include <stdexcept>
#include <cstdint>

namespace ghost::platform
{
class MenuServiceProcess
{
    HANDLE process{}, job{};
    ULONGLONG retry_at{};
    void cleanup()
    {
        if (job) CloseHandle(job);
        if (process) CloseHandle(process);
        job = process = nullptr;
    }

public:
    ~MenuServiceProcess()
    {
        cleanup();
    }
    void start(const std::filesystem::path& mod, const std::filesystem::path& game)
    {
        if (process)
        {
            DWORD code{};
            if (GetExitCodeProcess(process, &code) && code == STILL_ACTIVE)
                return;
            cleanup();
        }
        const auto now = GetTickCount64();
        if (now < retry_at)
            return;
        retry_at = now + 5000;
        const auto root = mod / L"bridge";
        wchar_t configured[32768]{};
        GetPrivateProfileStringW(L"Runtime", L"PythonPath", L"", configured, 32768,
                                 (root / L"runtime.ini").c_str());
        auto python = *configured ? std::filesystem::path(configured) : root / L"runtime/python/pythonw.exe";
        if (!std::filesystem::exists(python))
            python = root / L"runtime/python/python.exe";
        const auto script = root / L"server/ah_ghost_relay.py";
        if (!std::filesystem::exists(python) || !std::filesystem::exists(script))
            throw std::runtime_error("Install the complete updated mod package to enable the lobby menu");
        auto quote = [](const auto& path) { return L"\"" + path.wstring() + L"\""; };
        std::wstring command = quote(python) + L" " + quote(script) + L" menu-service --state-dir " +
                               quote(mod / L"state") + L" --game-path " + quote(game) +
                               L" --parent-pid " + std::to_wstring(GetCurrentProcessId());
        job = CreateJobObjectW(nullptr, nullptr);
        if (!job)
            throw std::runtime_error("Could not create lobby service job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        {
            cleanup();
            throw std::runtime_error("Could not configure lobby service lifetime");
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(python.c_str(), command.data(), nullptr, nullptr, false,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, root.c_str(), &startup, &child))
        {
            cleanup();
            throw std::runtime_error("Could not start the background lobby service");
        }
        process = child.hProcess;
        if (!AssignProcessToJobObject(job, process))
        {
            TerminateProcess(process, 1);
            CloseHandle(child.hThread);
            cleanup();
            throw std::runtime_error("Could not attach lobby service to the game lifetime");
        }
        const auto resumed = ResumeThread(child.hThread);
        CloseHandle(child.hThread);
        if (resumed == static_cast<DWORD>(-1))
        {
            cleanup();
            throw std::runtime_error("Could not resume the background lobby service");
        }
    }
};

} // namespace ghost::platform
