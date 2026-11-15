#include "PeaceDriverLoad.h"

#include <cwchar>
#include <cstdarg>
#include "PeaceDriverAPI.h"
#include "helper/Utils.h"

namespace {

using NTSTATUS = LONG;

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, * PUNICODE_STRING;

using PFN_NtLoadDriver = NTSTATUS(WINAPI*)(PUNICODE_STRING DriverServiceName);

constexpr wchar_t kPeaceDriverServicePrefix[] = L"PeaceDRV";
constexpr size_t kPeaceDriverServicePrefixLen = 8; // wcslen(L"PeaceDRV")

void InitUnicodeString(UNICODE_STRING* ustr, PWSTR str)
{
    const USHORT byteLen = static_cast<USHORT>(wcslen(str) * sizeof(WCHAR));
    ustr->Length = byteLen;
    ustr->MaximumLength = static_cast<USHORT>(byteLen + sizeof(WCHAR));
    ustr->Buffer = str;
}

PeaceDriverLoad::LoadDriverResult MakeLoadResult(bool success, LPCWSTR text)
{
    PeaceDriverLoad::LoadDriverResult result{};
    result.success = success;
    if (text)
        wcscpy_s(result.message, text);
    return result;
}

PeaceDriverLoad::LoadDriverResult MakeLoadResultFormat(bool success, LPCWSTR format, ...)
{
    PeaceDriverLoad::LoadDriverResult result{};
    result.success = success;
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(result.message, _countof(result.message), _TRUNCATE, format, args);
    va_end(args);
    return result;
}

LPCWSTR NtLoadStatusToReason(DWORD status)
{
    switch (status)
    {
    case static_cast<DWORD>(STATUS_UNSUPPORTPLATFORM):
        return L"CPU 平台不支持虚拟化";
    case static_cast<DWORD>(STATUS_UNSUPPORTVMX):
        return L"CPU 不支持 VMX 虚拟化功能";
    case static_cast<DWORD>(STATUS_UNENABLEVURTUAL):
        return L"CPU 未开启虚拟化功能";
    case static_cast<DWORD>(STATUS_UNENABLEBIOSVT):
        return L"BIOS 中未开启 VT 功能";
    case static_cast<DWORD>(STATUS_VMXENALED):
        return L"CPU 虚拟化已被占用";
    case static_cast<DWORD>(STATUS_VT_START_FAILED):
        return L"虚拟化开启失败";
    case static_cast<DWORD>(STATUS_KERNELSTART_FAILED):
        return L"KernelStart 初始化失败";
    case static_cast<DWORD>(STATUS_SYSTEMSTART_FAILED):
        return L"SystemStart 初始化失败";
    case static_cast<DWORD>(STATUS_MOU_CALLBACK_FAILED):
        return L"鼠标回调注册失败";
    case static_cast<DWORD>(STATUS_KDB_CALLBACK_FAILED):
        return L"键盘回调注册失败";
    case static_cast<DWORD>(STATUS_HV_PING_FAILED):
        return L"Hypervisor 通讯测试失败";
    case static_cast<DWORD>(STATUS_REG_CALLBACK_FAILED):
        return L"注册表回调注册失败";
    case static_cast<DWORD>(STATUS_SHUTDOWN_CALLBACK_FAILED):
        return L"关机回调注册失败";
    case static_cast<DWORD>(STATUS_OB_CALLBACK_FAILED):
        return L"进程保护回调注册失败";
    case 0xC0000022u: // STATUS_ACCESS_DENIED
        return L"ObRegisterCallbacks 被拒绝：驱动需 /INTEGRITYCHECK 链接并正确签名";
    case 0xC0000451u: // STATUS_FLT_INSTANCE_ALTITUDE_COLLISION
        return L"ObRegisterCallbacks 失败：Altitude 与其他驱动冲突";
    case 0xC000000Du: // STATUS_INVALID_PARAMETER
        return L"ObRegisterCallbacks 失败：注册参数无效";
    case 0xC0000020u: // STATUS_INVALID_FILE_FOR_SECTION
        return L"ImagePath 不是有效的驱动 PE 文件（路径可能指向目录或文件损坏）";
    case 0xC0000428u: // STATUS_INVALID_IMAGE_HASH
        return L"驱动未通过签名验证，请开启测试签名或以管理员安装测试证书";
    default:
        return nullptr;
    }
}

LPCWSTR Win32InstallErrorToReason(DWORD err)
{
    switch (err)
    {
    case ERROR_SERVICE_EXISTS:
        return L"服务已存在，请先执行 sc stop PeaceDriver && sc delete PeaceDriver";
    case ERROR_ACCESS_DENIED:
        return L"权限不足，请以管理员身份运行";
    case ERROR_INVALID_NAME:
        return L"服务名称无效";
    case ERROR_INVALID_PARAMETER:
        return L"安装参数无效";
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return L"驱动文件路径无效";
    default:
        return nullptr;
    }
}

bool IsPeaceDriverServiceName(LPCWSTR serviceName)
{
    if (!serviceName || serviceName[0] == L'\0')
        return false;
    return _wcsnicmp(serviceName, kPeaceDriverServicePrefix, kPeaceDriverServicePrefixLen) == 0;
}

bool GetLocalAppDataDir(wchar_t* driverDir, size_t driverDirChars, DWORD* outError)
{
    if (!driverDir || driverDirChars == 0)
    {
        if (outError)
            *outError = ERROR_INVALID_PARAMETER;
        return false;
    }

    const DWORD localAppDataLen = GetEnvironmentVariableW(
        L"LOCALAPPDATA", driverDir, static_cast<DWORD>(driverDirChars));
    if (localAppDataLen == 0 || localAppDataLen >= driverDirChars)
    {
        if (outError)
            *outError = GetLastError();
        return false;
    }

    size_t dirLen = wcslen(driverDir);
    if (dirLen > 0 && driverDir[dirLen - 1] != L'\\')
    {
        if (dirLen + 1 >= driverDirChars)
        {
            if (outError)
                *outError = ERROR_BUFFER_OVERFLOW;
            return false;
        }
        driverDir[dirLen] = L'\\';
        driverDir[dirLen + 1] = L'\0';
    }

    if (outError)
        *outError = 0;
    return true;
}

// 从 ImagePath（可能带 \\??\\ 前缀）得到可给 DeleteFileW 使用的路径
void NormalizeImagePathForDelete(LPCWSTR imagePath, wchar_t* outPath, size_t outPathChars)
{
    if (!outPath || outPathChars == 0)
        return;
    outPath[0] = L'\0';
    if (!imagePath || imagePath[0] == L'\0')
        return;

    if (wcsncmp(imagePath, L"\\??\\", 4) == 0)
        wcsncpy_s(outPath, outPathChars, imagePath + 4, _TRUNCATE);
    else
        wcsncpy_s(outPath, outPathChars, imagePath, _TRUNCATE);
}

bool QueryServiceImagePath(SC_HANDLE scService, wchar_t* imagePath, size_t imagePathChars)
{
    if (!scService || !imagePath || imagePathChars == 0)
        return false;

    imagePath[0] = L'\0';
    DWORD bytesNeeded = 0;
    QueryServiceConfigW(scService, nullptr, 0, &bytesNeeded);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytesNeeded == 0)
        return false;

    auto* config = static_cast<LPQUERY_SERVICE_CONFIGW>(HeapAlloc(GetProcessHeap(), 0, bytesNeeded));
    if (!config)
        return false;

    const BOOL ok = QueryServiceConfigW(scService, config, bytesNeeded, &bytesNeeded);
    if (ok && config->lpBinaryPathName)
        wcsncpy_s(imagePath, imagePathChars, config->lpBinaryPathName, _TRUNCATE);

    HeapFree(GetProcessHeap(), 0, config);
    return ok && imagePath[0] != L'\0';
}


} // namespace

PeaceDriverLoad::PeaceDriverLoad() : m_errorCode(0) {}

BOOL PeaceDriverLoad::enableDrvLoadPrivilege()
{
    HANDLE hToken;
    LUID sedebugnameValue;
    TOKEN_PRIVILEGES tkp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
    {
        return  FALSE;
    }
    if (!LookupPrivilegeValue(NULL, SE_LOAD_DRIVER_NAME, &sedebugnameValue))
    {
        CloseHandle(hToken);
        return FALSE;
    }
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Luid = sedebugnameValue;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL))
    {
        CloseHandle(hToken);
        return FALSE;
    }
    return TRUE;
}

BOOL PeaceDriverLoad::installDriver(LPCWSTR ServiceName, LPCWSTR DriverPath)
{
    SC_HANDLE sc_manage = NULL;
    SC_HANDLE sc_service = NULL;
    BOOL bRet = FALSE;
    m_errorCode = 0;

    do
    {
        sc_manage = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
        if (NULL == sc_manage)
        {
            m_errorCode = GetLastError();
            break;
        }

        sc_service = CreateService(
            sc_manage,
            ServiceName,
            ServiceName,
            SERVICE_ALL_ACCESS,
            SERVICE_KERNEL_DRIVER,
            SERVICE_DEMAND_START, // 手动启动
            // SERVICE_AUTO_START, // 开机自启
            SERVICE_ERROR_IGNORE,
            DriverPath,
            NULL, NULL, NULL, NULL, NULL);

        if (NULL == sc_service)
        {
            m_errorCode = GetLastError();
            break;
        }

        bRet = TRUE;
    } while (0);

    if (sc_service != NULL)
    {
        CloseServiceHandle(sc_service);
        sc_service = NULL;
    }

    if (sc_manage != NULL)
    {
        CloseServiceHandle(sc_manage);
        sc_manage = NULL;
    }

    return bRet;
}

BOOL PeaceDriverLoad::startDriver(LPCWSTR ServiceName)
{
    SC_HANDLE sc_manage = NULL;
    SC_HANDLE sc_service = NULL;
    BOOL bRet = FALSE;
    m_errorCode = 0;

    do
    {
        sc_manage = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (NULL == sc_manage)
        {
            m_errorCode = GetLastError();
            break;
        }

        sc_service = OpenService(sc_manage, ServiceName, SERVICE_ALL_ACCESS);
        if (NULL == sc_service)
        {
            m_errorCode = GetLastError();
            break;
        }

        if (!StartService(sc_service, 0, NULL))
        {
            const DWORD lastError = GetLastError();
            SERVICE_STATUS status = {};

            // StartService 失败时 GetLastError 多为 SCM 层错误；
            // 内核驱动加载失败时 DriverEntry 的 NTSTATUS 通常在 dwWin32ExitCode 中。
            if (QueryServiceStatus(sc_service, &status))
            {
                if (status.dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR)
                    m_errorCode = status.dwServiceSpecificExitCode;
                else if (status.dwWin32ExitCode != NO_ERROR)
                    m_errorCode = status.dwWin32ExitCode;
                else
                    m_errorCode = lastError;
            }
            else
            {
                m_errorCode = lastError;
            }
            break;
        }

        bRet = TRUE;
    } while (0);

    if (sc_service != NULL)
    {
        CloseServiceHandle(sc_service);
        sc_service = NULL;
    }

    if (sc_manage != NULL)
    {
        CloseServiceHandle(sc_manage);
        sc_manage = NULL;
    }

    return bRet;
}

BOOL PeaceDriverLoad::startDriverNt(LPCWSTR ServiceName)
{
    m_errorCode = 0;

    if (!ServiceName || ServiceName[0] == L'\0')
    {
        m_errorCode = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    if (!enableDrvLoadPrivilege())
    {
        m_errorCode = GetLastError();
        return FALSE;
    }

    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
    {
        m_errorCode = GetLastError();
        return FALSE;
    }

    const auto NtLoadDriverFn = reinterpret_cast<PFN_NtLoadDriver>(
        GetProcAddress(ntdll, "NtLoadDriver"));
    if (!NtLoadDriverFn)
    {
        m_errorCode = ERROR_PROC_NOT_FOUND;
        return FALSE;
    }

    wchar_t regPath[512] = {};
    if (_snwprintf_s(regPath, _TRUNCATE,
                     L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\%s",
                     ServiceName) <= 0)
    {
        m_errorCode = ERROR_BUFFER_OVERFLOW;
        return FALSE;
    }

    UNICODE_STRING driverServiceName = {};
    InitUnicodeString(&driverServiceName, regPath);

    const NTSTATUS status = NtLoadDriverFn(&driverServiceName);
    m_errorCode = static_cast<DWORD>(status);

    // 驱动已加载视为成功
    if (status == static_cast<NTSTATUS>(0xC000010EL)) // STATUS_IMAGE_ALREADY_LOADED
        return TRUE;

    return NT_SUCCESS(status) ? TRUE : FALSE;
}

BOOL PeaceDriverLoad::stopDriver(LPCWSTR ServiceName)
{
    SC_HANDLE sc_manage = NULL;
    SC_HANDLE sc_service = NULL;
    SERVICE_STATUS ss = { 0 };
    BOOL bRet = FALSE;

    do
    {
        sc_manage = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (NULL == sc_manage)
        {
            m_errorCode = GetLastError();
            break;
        }

        sc_service = OpenService(sc_manage, ServiceName, SERVICE_ALL_ACCESS);
        if (NULL == sc_service)
        {
            m_errorCode = GetLastError();
            break;
        }

        if (ControlService(sc_service, SERVICE_CONTROL_STOP, &ss) == FALSE)
        {
            m_errorCode = GetLastError();
            break;
        }

        bRet = TRUE;
    } while (0);

    if (sc_service != NULL)
    {
        CloseServiceHandle(sc_service);
        sc_service = NULL;
    }

    if (sc_manage != NULL)
    {
        CloseServiceHandle(sc_manage);
        sc_manage = NULL;
    }

    return bRet;
}

BOOL PeaceDriverLoad::unInstallDriver(LPCWSTR ServiceName)
{
    SC_HANDLE sc_manage = NULL;
    SC_HANDLE sc_service = NULL;
    BOOL bRet = FALSE;

    do
    {
        sc_manage = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (sc_manage == NULL)
        {
            m_errorCode = GetLastError();
            break;
        }

        sc_service = OpenService(sc_manage, ServiceName, SERVICE_ALL_ACCESS);
        if (sc_service == NULL)
        {
            m_errorCode = GetLastError();
            break;
        }

        if (DeleteService(sc_service) == FALSE)
        {
            m_errorCode = GetLastError();
            break;
        }

        bRet = TRUE;
    } while (0);

    if (sc_service != NULL)
    {
        CloseServiceHandle(sc_service);
        sc_service = NULL;
    }

    if (sc_manage != NULL)
    {
        CloseServiceHandle(sc_manage);
        sc_manage = NULL;
    }

    return bRet;
}

PeaceDriverLoad::LoadDriverResult PeaceDriverLoad::loadPeaceDriver()
{
    if (PeaceDriver::pingREG())
        return MakeLoadResult(true, L"驱动安装成功：已存在运行中的驱动");

    wchar_t randSuffix[64] = {};
    if (!Utils::makeRandName(randSuffix, _countof(randSuffix))) {
        return MakeLoadResultFormat(false, L"驱动安装失败：生成随机驱动名失败");
    }

    wchar_t driver_name[64] = {};
    _snwprintf_s(driver_name, _TRUNCATE, L"PeaceDRV%s", randSuffix);

    wchar_t driver_dir[MAX_PATH] = {};
    const DWORD localAppDataLen = GetEnvironmentVariableW(L"LOCALAPPDATA", driver_dir, MAX_PATH);
    if (localAppDataLen == 0 || localAppDataLen >= MAX_PATH) {
        return MakeLoadResultFormat(false, L"驱动安装失败：获取 AppData\\Local 路径失败，错误码: %lu", GetLastError());
    }

    size_t dirLen = wcslen(driver_dir);
    if (dirLen > 0 && driver_dir[dirLen - 1] != L'\\') {
        if (dirLen + 1 >= MAX_PATH) {
            return MakeLoadResultFormat(false, L"驱动安装失败：AppData\\Local 路径过长");
        }
        driver_dir[dirLen] = L'\\';
        driver_dir[dirLen + 1] = L'\0';
    }

    wchar_t driver_path[MAX_PATH] = {};
    _snwprintf_s(driver_path, _TRUNCATE, L"%s%s.sys", driver_dir, driver_name);

    wchar_t sysMessage[512] = {};
    if (!Utils::makeSysFile(driver_dir, driver_name, sysMessage, _countof(sysMessage))) {
        return MakeLoadResultFormat(false, L"驱动安装失败：导出驱动文件失败 (%s)，", sysMessage);
    }

    Utils::PrintLineW(sysMessage);

    wchar_t drvPathBuf[MAX_PATH] = {};
    const DWORD pathLen = GetFullPathNameW(driver_path, MAX_PATH, drvPathBuf, nullptr);
    if (pathLen == 0 || pathLen >= MAX_PATH)
    {
        const DWORD err = GetLastError();
        return MakeLoadResultFormat(false, L"驱动安装失败：驱动路径无效 (%s)，错误码 %lu", driver_path, err);
    }

    const DWORD fileAttrs = GetFileAttributesW(drvPathBuf);
    if (fileAttrs == INVALID_FILE_ATTRIBUTES || (fileAttrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        const DWORD err = GetLastError();
        return MakeLoadResultFormat(false, L"驱动安装失败：驱动文件不存在 (%s)，错误码 %lu", drvPathBuf, err);
    }

    wchar_t imagePath[MAX_PATH + 8] = {};
    if (wcsncmp(drvPathBuf, L"\\??\\", 4) == 0)
        wcscpy_s(imagePath, drvPathBuf);
    else if (_snwprintf_s(imagePath, _TRUNCATE, L"\\??\\%s", drvPathBuf) <= 0)
        return MakeLoadResult(false, L"驱动安装失败：构造 ImagePath 失败");

    SC_HANDLE sc_manage = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (sc_manage)
    {
        SC_HANDLE sc_service = OpenServiceW(sc_manage, driver_name, SERVICE_QUERY_STATUS);
        if (sc_service)
        {
            CloseServiceHandle(sc_service);
            stopDriver(driver_name);
            if (!unInstallDriver(driver_name))
            {
                const DWORD err = getErrorCode();
                if (LPCWSTR reason = Win32InstallErrorToReason(err))
                {
                    return MakeLoadResultFormat(false, L"驱动安装失败：卸载已有服务失败，%s (0x%08X)", reason, err);
                }
                return MakeLoadResultFormat(false, L"驱动安装失败：卸载已有服务失败，错误码 0x%08X (%lu)", err, err);
            }
        }
        CloseServiceHandle(sc_manage);
    }

    if (!installDriver(driver_name, imagePath))
    {
        const DWORD err = getErrorCode();
        if (LPCWSTR reason = Win32InstallErrorToReason(err))
        {
            return MakeLoadResultFormat(false, L"驱动安装失败：%s (0x%08X)", reason, err);
        }
        return MakeLoadResultFormat(false, L"驱动安装失败：CreateService 失败，错误码 0x%08X (%lu)", err, err);
    }

    if (!startDriverNt(driver_name))
    {
        const DWORD err_code = getErrorCode();
        if (LPCWSTR reason = NtLoadStatusToReason(err_code))
        {
            stopDriver(driver_name);
            unInstallDriver(driver_name);
            return MakeLoadResultFormat(false, L"驱动安装失败：启动失败，%s (NTSTATUS 0x%08X)", reason, err_code);
        }

        stopDriver(driver_name);
        unInstallDriver(driver_name);
        return MakeLoadResultFormat(false, L"驱动安装失败：启动失败，NTSTATUS 0x%08X (%lu)", err_code, err_code);
    }

    if (!PeaceDriver::pingREG())
    {
        stopDriver(driver_name);
        unInstallDriver(driver_name);
        return MakeLoadResult(false, L"驱动安装失败：驱动已启动，但未能与驱动成功通讯");
    }

    return MakeLoadResult(true, L"驱动安装成功：驱动安装流程已执行完毕");
}

PeaceDriverLoad::LoadDriverResult PeaceDriverLoad::unloadPeaceDriver()
{
    wchar_t driver_dir[MAX_PATH] = {};
    DWORD pathErr = 0;
    if (!GetLocalAppDataDir(driver_dir, _countof(driver_dir), &pathErr))
    {
        return MakeLoadResultFormat(false, L"驱动卸载失败：获取 AppData\\Local 路径失败，错误码: %lu", pathErr);
    }

    SC_HANDLE sc_manage = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE | SC_MANAGER_CONNECT);
    if (!sc_manage)
    {
        const DWORD err = GetLastError();
        if (LPCWSTR reason = Win32InstallErrorToReason(err))
            return MakeLoadResultFormat(false, L"驱动卸载失败：打开服务管理器失败，%s (0x%08X)", reason, err);
        return MakeLoadResultFormat(false, L"驱动卸载失败：打开服务管理器失败，错误码 0x%08X (%lu)", err, err);
    }

    DWORD bytesNeeded = 0;
    DWORD servicesReturned = 0;
    DWORD resumeHandle = 0;
    EnumServicesStatusExW(
        sc_manage,
        SC_ENUM_PROCESS_INFO,
        SERVICE_DRIVER,
        SERVICE_STATE_ALL,
        nullptr,
        0,
        &bytesNeeded,
        &servicesReturned,
        &resumeHandle,
        nullptr);

    if (GetLastError() != ERROR_MORE_DATA || bytesNeeded == 0)
    {
        CloseServiceHandle(sc_manage);
        if (!PeaceDriver::pingREG())
            return MakeLoadResult(true, L"驱动卸载成功：当前没有已加载的驱动");
        return MakeLoadResult(false, L"驱动卸载失败：枚举驱动服务失败，且驱动仍在运行");
    }

    auto* services = static_cast<LPENUM_SERVICE_STATUS_PROCESSW>(
        HeapAlloc(GetProcessHeap(), 0, bytesNeeded));
    if (!services)
    {
        CloseServiceHandle(sc_manage);
        return MakeLoadResult(false, L"驱动卸载失败：分配服务枚举缓冲区失败");
    }

    resumeHandle = 0;
    const BOOL enumOk = EnumServicesStatusExW(
        sc_manage,
        SC_ENUM_PROCESS_INFO,
        SERVICE_DRIVER,
        SERVICE_STATE_ALL,
        reinterpret_cast<LPBYTE>(services),
        bytesNeeded,
        &bytesNeeded,
        &servicesReturned,
        &resumeHandle,
        nullptr);

    if (!enumOk)
    {
        const DWORD err = GetLastError();
        HeapFree(GetProcessHeap(), 0, services);
        CloseServiceHandle(sc_manage);
        return MakeLoadResultFormat(false, L"驱动卸载失败：枚举驱动服务失败，错误码 0x%08X (%lu)", err, err);
    }

    int matchedCount = 0;
    int cleanedCount = 0;
    DWORD lastFailCode = 0;
    wchar_t lastFailName[64] = {};

    for (DWORD i = 0; i < servicesReturned; ++i)
    {
        LPCWSTR serviceName = services[i].lpServiceName;
        if (!IsPeaceDriverServiceName(serviceName))
            continue;

        ++matchedCount;

        wchar_t imagePath[MAX_PATH + 8] = {};
        SC_HANDLE sc_service = OpenServiceW(sc_manage, serviceName, SERVICE_QUERY_CONFIG);
        if (sc_service)
        {
            QueryServiceImagePath(sc_service, imagePath, _countof(imagePath));
            CloseServiceHandle(sc_service);
        }

        // 已停止视为可继续卸载
        if (!stopDriver(serviceName))
        {
            const DWORD err = getErrorCode();
            if (err != ERROR_SERVICE_NOT_ACTIVE)
            {
                lastFailCode = err;
                wcsncpy_s(lastFailName, serviceName, _TRUNCATE);
                continue;
            }
        }

        if (!unInstallDriver(serviceName))
        {
            const DWORD err = getErrorCode();
            if (err != ERROR_SERVICE_DOES_NOT_EXIST)
            {
                lastFailCode = err;
                wcsncpy_s(lastFailName, serviceName, _TRUNCATE);
                continue;
            }
        }

        wchar_t filePath[MAX_PATH] = {};
        if (imagePath[0] != L'\0')
        {
            NormalizeImagePathForDelete(imagePath, filePath, _countof(filePath));
        }
        else
        {
            _snwprintf_s(filePath, _TRUNCATE, L"%s%s.sys", driver_dir, serviceName);
        }

        if (filePath[0] != L'\0')
            DeleteFileW(filePath);

        ++cleanedCount;
    }

    HeapFree(GetProcessHeap(), 0, services);
    CloseServiceHandle(sc_manage);

    // 再清理 LocalAppData 中可能残留的 PeaceDRV*.sys
    wchar_t findPattern[MAX_PATH] = {};
    _snwprintf_s(findPattern, _TRUNCATE, L"%s%s*.sys", driver_dir, kPeaceDriverServicePrefix);
    WIN32_FIND_DATAW findData = {};
    HANDLE findHandle = FindFirstFileW(findPattern, &findData);
    if (findHandle != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            wchar_t leftoverPath[MAX_PATH] = {};
            _snwprintf_s(leftoverPath, _TRUNCATE, L"%s%s", driver_dir, findData.cFileName);
            DeleteFileW(leftoverPath);
        } while (FindNextFileW(findHandle, &findData));
        FindClose(findHandle);
    }

    if (matchedCount == 0)
    {
        if (!PeaceDriver::pingREG())
            return MakeLoadResult(true, L"驱动卸载成功：当前没有已加载的驱动");
        return MakeLoadResult(false, L"驱动卸载失败：驱动仍在运行，但未找到 PeaceDRV* 服务");
    }

    if (cleanedCount == 0)
    {
        if (LPCWSTR reason = Win32InstallErrorToReason(lastFailCode))
        {
            return MakeLoadResultFormat(false,
                                        L"驱动卸载失败：卸载服务 %s 失败，%s (0x%08X)",
                                        lastFailName, reason, lastFailCode);
        }
        return MakeLoadResultFormat(false,
                                    L"驱动卸载失败：卸载服务 %s 失败，错误码 0x%08X (%lu)",
                                    lastFailName, lastFailCode, lastFailCode);
    }

    if (PeaceDriver::pingREG())
    {
        return MakeLoadResult(false, L"驱动卸载失败：服务已处理，但未能断开与驱动的通讯");
    }

    if (cleanedCount < matchedCount)
    {
        return MakeLoadResultFormat(true,
                                    L"驱动卸载成功：已清理 %d/%d 个 PeaceDRV 服务（部分服务清理失败）",
                                    cleanedCount, matchedCount);
    }

    return MakeLoadResultFormat(true, L"驱动卸载成功：已清理 %d 个 PeaceDRV 服务", cleanedCount);
}

BOOL PeaceDriverLoad::driverIsLoaded()
{
    return PeaceDriver::pingREG();
}

LPCWSTR PeaceDriverLoad::NtLoadStatusToReason(DWORD status)
{
    switch (status)
    {
    case static_cast<DWORD>(STATUS_UNSUPPORTPLATFORM):
        return L"CPU 平台不支持虚拟化";
    case static_cast<DWORD>(STATUS_UNSUPPORTVMX):
        return L"CPU 不支持 VMX 虚拟化功能";
    case static_cast<DWORD>(STATUS_UNENABLEVURTUAL):
        return L"CPU 未开启虚拟化功能";
    case static_cast<DWORD>(STATUS_UNENABLEBIOSVT):
        return L"BIOS 中未开启 VT 功能";
    case static_cast<DWORD>(STATUS_VMXENALED):
        return L"CPU 虚拟化已被占用";
    case static_cast<DWORD>(STATUS_VT_START_FAILED):
        return L"虚拟化开启失败";
    case static_cast<DWORD>(STATUS_KERNELSTART_FAILED):
        return L"KernelStart 初始化失败";
    case static_cast<DWORD>(STATUS_SYSTEMSTART_FAILED):
        return L"SystemStart 初始化失败";
    case static_cast<DWORD>(STATUS_MOU_CALLBACK_FAILED):
        return L"鼠标回调注册失败";
    case static_cast<DWORD>(STATUS_KDB_CALLBACK_FAILED):
        return L"键盘回调注册失败";
    case static_cast<DWORD>(STATUS_HV_PING_FAILED):
        return L"Hypervisor 通讯测试失败";
    case static_cast<DWORD>(STATUS_REG_CALLBACK_FAILED):
        return L"注册表回调注册失败";
    case static_cast<DWORD>(STATUS_SHUTDOWN_CALLBACK_FAILED):
        return L"关机回调注册失败";
    case static_cast<DWORD>(STATUS_OB_CALLBACK_FAILED):
        return L"进程保护回调注册失败";
    case 0xC0000022u: // STATUS_ACCESS_DENIED
        return L"ObRegisterCallbacks 被拒绝：驱动需 /INTEGRITYCHECK 链接并正确签名";
    case 0xC0000451u: // STATUS_FLT_INSTANCE_ALTITUDE_COLLISION
        return L"ObRegisterCallbacks 失败：Altitude 与其他驱动冲突";
    case 0xC000000Du: // STATUS_INVALID_PARAMETER
        return L"ObRegisterCallbacks 失败：注册参数无效";
    case 0xC0000020u: // STATUS_INVALID_FILE_FOR_SECTION
        return L"ImagePath 不是有效的驱动 PE 文件（路径可能指向目录或文件损坏）";
    case 0xC0000428u: // STATUS_INVALID_IMAGE_HASH
        return L"驱动未通过签名验证，请开启测试签名或以管理员安装测试证书";
    default:
        return nullptr;
    }
}
