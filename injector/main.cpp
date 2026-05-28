#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace
{
    std::wstring Widen(const std::string& value)
    {
        if (value.empty())
        {
            return {};
        }

        int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        if (length <= 0)
        {
            return std::wstring(value.begin(), value.end());
        }

        std::wstring result(length - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), length);
        return result;
    }

    std::wstring ProcessNameWithoutExtension(std::wstring processName)
    {
        if (processName.ends_with(L".exe") || processName.ends_with(L".EXE"))
        {
            processName.resize(processName.size() - 4);
        }
        return processName;
    }

    bool ProcessNameMatches(const std::wstring& candidate, const std::wstring& requested)
    {
        return candidate == requested || ProcessNameWithoutExtension(candidate) == ProcessNameWithoutExtension(requested);
    }

    DWORD GetProcessIdByName(const std::wstring& processName)
    {
        DWORD pid = 0;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE)
        {
            PROCESSENTRY32W processEntry{};
            processEntry.dwSize = sizeof(processEntry);
            if (Process32FirstW(snapshot, &processEntry))
            {
                do
                {
                    if (ProcessNameMatches(processEntry.szExeFile, processName))
                    {
                        pid = processEntry.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(snapshot, &processEntry));
            }
            CloseHandle(snapshot);
        }
        return pid;
    }

    DWORD WaitForProcess(const std::wstring& processName)
    {
        std::wcout << L"Waiting for process: " << processName << L"..." << std::endl;
        while (true)
        {
            DWORD pid = GetProcessIdByName(processName);
            if (pid != 0)
            {
                return pid;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    std::filesystem::path ResolveDllPath(const std::filesystem::path& requestedPath, const char* executablePath)
    {
        if (std::filesystem::exists(requestedPath))
        {
            return std::filesystem::absolute(requestedPath);
        }

        const std::filesystem::path fileName = requestedPath.filename().empty() ? "wowiezz.dll" : requestedPath.filename();
        const std::filesystem::path executableDirectory = std::filesystem::absolute(std::filesystem::path(executablePath)).parent_path();
        const std::filesystem::path currentDirectory = std::filesystem::current_path();

        const std::filesystem::path candidates[] = {
            executableDirectory / fileName,
            executableDirectory / ".download" / fileName,
            currentDirectory / fileName,
            currentDirectory / ".download" / fileName,
            currentDirectory / "build" / "windows" / "x64" / "release" / fileName,
            currentDirectory / "build" / "windows" / "x64" / "debug" / fileName,
        };

        for (const auto& candidate : candidates)
        {
            if (std::filesystem::exists(candidate))
            {
                return std::filesystem::absolute(candidate);
            }
        }

        return std::filesystem::absolute(requestedPath);
    }

    bool InjectDLL(DWORD pid, const std::filesystem::path& dllPath)
    {
        if (!std::filesystem::exists(dllPath))
        {
            std::wcerr << L"DLL not found: " << dllPath.wstring() << std::endl;
            return false;
        }

        constexpr DWORD processAccess = PROCESS_CREATE_THREAD |
                                        PROCESS_QUERY_INFORMATION |
                                        PROCESS_VM_OPERATION |
                                        PROCESS_VM_WRITE |
                                        PROCESS_VM_READ;

        HANDLE process = OpenProcess(processAccess, FALSE, pid);
        if (!process)
        {
            std::cerr << "Failed to open process (try running as administrator): " << GetLastError() << std::endl;
            return false;
        }

        const std::wstring absoluteDllPath = std::filesystem::absolute(dllPath).wstring();
        const SIZE_T dllPathBytes = (absoluteDllPath.length() + 1) * sizeof(wchar_t);
        void* allocatedMemory = VirtualAllocEx(process, nullptr, dllPathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!allocatedMemory)
        {
            std::cerr << "Failed to allocate memory: " << GetLastError() << std::endl;
            CloseHandle(process);
            return false;
        }

        SIZE_T bytesWritten = 0;
        if (!WriteProcessMemory(process, allocatedMemory, absoluteDllPath.c_str(), dllPathBytes, &bytesWritten) || bytesWritten != dllPathBytes)
        {
            std::cerr << "Failed to write DLL path: " << GetLastError() << std::endl;
            VirtualFreeEx(process, allocatedMemory, 0, MEM_RELEASE);
            CloseHandle(process);
            return false;
        }

        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        FARPROC loadLibrary = kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr;
        if (!loadLibrary)
        {
            std::cerr << "Failed to find LoadLibraryW: " << GetLastError() << std::endl;
            VirtualFreeEx(process, allocatedMemory, 0, MEM_RELEASE);
            CloseHandle(process);
            return false;
        }

        HANDLE thread = CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary), allocatedMemory, 0, nullptr);
        if (!thread)
        {
            std::cerr << "Failed to create remote thread: " << GetLastError() << std::endl;
            VirtualFreeEx(process, allocatedMemory, 0, MEM_RELEASE);
            CloseHandle(process);
            return false;
        }

        DWORD waitResult = WaitForSingleObject(thread, INFINITE);
        DWORD exitCode = 0;
        const bool hasExitCode = GetExitCodeThread(thread, &exitCode);

        VirtualFreeEx(process, allocatedMemory, 0, MEM_RELEASE);
        CloseHandle(thread);
        CloseHandle(process);

        if (waitResult != WAIT_OBJECT_0)
        {
            std::cerr << "Timed out or failed while waiting for LoadLibraryW. Wait result: " << waitResult << std::endl;
            return false;
        }

        if (!hasExitCode)
        {
            std::cerr << "Failed to read remote thread exit code: " << GetLastError() << std::endl;
            return false;
        }

        if (exitCode == 0)
        {
            std::cerr << "LoadLibraryW failed inside the target process. Check DLL architecture and dependencies." << std::endl;
            return false;
        }

        return true;
    }
}

int main(int argc, char* argv[])
{
    std::wstring targetProcess = L"Polytoria Client.exe";
    std::filesystem::path dllPath = "wowiezz.dll";

    if (argc >= 2)
    {
        targetProcess = Widen(argv[1]);
    }

    if (argc >= 3)
    {
        dllPath = Widen(argv[2]);
    }

    dllPath = ResolveDllPath(dllPath, argv[0]);

    DWORD pid = GetProcessIdByName(targetProcess);
    if (pid == 0)
    {
        pid = WaitForProcess(targetProcess);
    }

    std::wcout << L"Found process " << targetProcess << L" with PID: " << pid << std::endl;
    std::wcout << L"Will inject DLL in 6 seconds: " << dllPath.wstring() << std::endl;

    Sleep(6000);
    if (InjectDLL(pid, dllPath))
    {
        std::cout << "Successfully injected!" << std::endl;
    }
    else
    {
        std::cerr << "Injection failed." << std::endl;
        return 1;
    }

    return 0;
}
