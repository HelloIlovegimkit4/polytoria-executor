using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace PolyHack
{
    public sealed class DllInjectionResult
    {
        private DllInjectionResult(bool success, string message)
        {
            Success = success;
            Message = message;
        }

        public bool Success { get; }
        public string Message { get; }

        public static DllInjectionResult Ok(string message) => new(true, message);
        public static DllInjectionResult Fail(string message) => new(false, message);
    }

    public static class DllInjector
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, UIntPtr dwSize, uint flAllocationType, uint flProtect);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool VirtualFreeEx(IntPtr hProcess, IntPtr lpAddress, UIntPtr dwSize, uint dwFreeType);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, UIntPtr nSize, out UIntPtr lpNumberOfBytesWritten);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
        private static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandle(string lpModuleName);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpThreadAttributes, UIntPtr dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetExitCodeThread(IntPtr hThread, out uint lpExitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr hObject);

        private const uint PROCESS_CREATE_THREAD = 0x0002;
        private const uint PROCESS_QUERY_INFORMATION = 0x0400;
        private const uint PROCESS_VM_OPERATION = 0x0008;
        private const uint PROCESS_VM_WRITE = 0x0020;
        private const uint PROCESS_VM_READ = 0x0010;
        private const uint MEM_COMMIT = 0x1000;
        private const uint MEM_RESERVE = 0x2000;
        private const uint MEM_RELEASE = 0x8000;
        private const uint PAGE_READWRITE = 0x04;
        private const uint WAIT_OBJECT_0 = 0x00000000;
        private const uint INFINITE = 0xFFFFFFFF;

        private const uint RequiredProcessAccess = PROCESS_CREATE_THREAD |
                                                   PROCESS_QUERY_INFORMATION |
                                                   PROCESS_VM_OPERATION |
                                                   PROCESS_VM_WRITE |
                                                   PROCESS_VM_READ;

        public static bool Inject(string processName, string dllPath)
        {
            return InjectDetailed(processName, dllPath).Success;
        }

        public static DllInjectionResult InjectDetailed(string processName, string dllPath)
        {
            if (string.IsNullOrWhiteSpace(processName))
            {
                return DllInjectionResult.Fail("No target process name was provided.");
            }

            if (string.IsNullOrWhiteSpace(dllPath))
            {
                return DllInjectionResult.Fail("No DLL path was provided.");
            }

            string fullDllPath = Path.GetFullPath(dllPath);
            if (!File.Exists(fullDllPath))
            {
                return DllInjectionResult.Fail($"DLL not found: {fullDllPath}");
            }

            Process? targetProcess = FindProcess(processName);
            if (targetProcess == null)
            {
                return DllInjectionResult.Fail($"Process not found: {processName}");
            }

            IntPtr hProcess = OpenProcess(RequiredProcessAccess, false, targetProcess.Id);
            if (hProcess == IntPtr.Zero)
            {
                return DllInjectionResult.Fail($"Failed to open {targetProcess.ProcessName} ({targetProcess.Id}). Try running PolyHack as administrator. Win32 error: {Marshal.GetLastWin32Error()}");
            }

            IntPtr remotePath = IntPtr.Zero;
            IntPtr hThread = IntPtr.Zero;
            try
            {
                byte[] dllPathBytes = Encoding.Unicode.GetBytes(fullDllPath + '\0');
                remotePath = VirtualAllocEx(hProcess, IntPtr.Zero, (UIntPtr)dllPathBytes.Length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (remotePath == IntPtr.Zero)
                {
                    return DllInjectionResult.Fail($"Failed to allocate memory in {targetProcess.ProcessName}. Win32 error: {Marshal.GetLastWin32Error()}");
                }

                if (!WriteProcessMemory(hProcess, remotePath, dllPathBytes, (UIntPtr)dllPathBytes.Length, out UIntPtr bytesWritten) || bytesWritten.ToUInt64() != (ulong)dllPathBytes.Length)
                {
                    return DllInjectionResult.Fail($"Failed to write DLL path into {targetProcess.ProcessName}. Win32 error: {Marshal.GetLastWin32Error()}");
                }

                IntPtr kernel32 = GetModuleHandle("kernel32.dll");
                if (kernel32 == IntPtr.Zero)
                {
                    return DllInjectionResult.Fail($"Failed to get kernel32.dll handle. Win32 error: {Marshal.GetLastWin32Error()}");
                }

                IntPtr loadLibrary = GetProcAddress(kernel32, "LoadLibraryW");
                if (loadLibrary == IntPtr.Zero)
                {
                    return DllInjectionResult.Fail($"Failed to find LoadLibraryW. Win32 error: {Marshal.GetLastWin32Error()}");
                }

                hThread = CreateRemoteThread(hProcess, IntPtr.Zero, UIntPtr.Zero, loadLibrary, remotePath, 0, IntPtr.Zero);
                if (hThread == IntPtr.Zero)
                {
                    return DllInjectionResult.Fail($"Failed to create remote thread in {targetProcess.ProcessName}. Win32 error: {Marshal.GetLastWin32Error()}");
                }

                uint waitResult = WaitForSingleObject(hThread, INFINITE);
                if (waitResult != WAIT_OBJECT_0)
                {
                    return DllInjectionResult.Fail($"Timed out or failed while waiting for LoadLibraryW. Wait result: {waitResult}");
                }

                if (!GetExitCodeThread(hThread, out uint exitCode))
                {
                    return DllInjectionResult.Fail($"Failed to read LoadLibraryW exit code. Win32 error: {Marshal.GetLastWin32Error()}");
                }

                if (exitCode == 0)
                {
                    return DllInjectionResult.Fail($"LoadLibraryW failed inside {targetProcess.ProcessName}; the DLL was not loaded. Check architecture, dependencies, and antivirus blocks.");
                }

                return DllInjectionResult.Ok($"Injected {Path.GetFileName(fullDllPath)} into {targetProcess.ProcessName} ({targetProcess.Id}).");
            }
            finally
            {
                if (hThread != IntPtr.Zero)
                {
                    CloseHandle(hThread);
                }

                if (remotePath != IntPtr.Zero)
                {
                    VirtualFreeEx(hProcess, remotePath, UIntPtr.Zero, MEM_RELEASE);
                }

                CloseHandle(hProcess);
            }
        }

        private static Process? FindProcess(string processName)
        {
            string normalizedName = Path.GetFileNameWithoutExtension(processName);
            Process? exactMatch = Process.GetProcessesByName(normalizedName).FirstOrDefault();
            if (exactMatch != null)
            {
                return exactMatch;
            }

            return Process.GetProcesses()
                .FirstOrDefault(process => string.Equals(process.ProcessName, normalizedName, StringComparison.OrdinalIgnoreCase) ||
                                           string.Equals(process.ProcessName + ".exe", processName, StringComparison.OrdinalIgnoreCase));
        }
    }
}
