#ifndef PEACEDRIVERLOAD_H
#define PEACEDRIVERLOAD_H

#include <windows.h>

#define STATUS_UNSUPPORTPLATFORM	((NTSTATUS)0xC0040100L) // CPU平台不支持
#define STATUS_UNSUPPORTVMX         ((NTSTATUS)0xC0040101L) // CPU不支持虚拟化
#define STATUS_UNENABLEVURTUAL      ((NTSTATUS)0xC0040102L) // CPU未开启虚拟化功能
#define STATUS_UNENABLEBIOSVT       ((NTSTATUS)0xC0040103L) // BIOS中未开启虚拟化功能
#define STATUS_VMXENALED            ((NTSTATUS)0xC0040104L) // CPU虚拟化被占用
#define STATUS_VT_START_FAILED      ((NTSTATUS)0xC0040105L) // 虚拟化开启失败
#define STATUS_KERNELSTART_FAILED   ((NTSTATUS)0xC0040106L) // KernelStart失败
#define STATUS_SYSTEMSTART_FAILED   ((NTSTATUS)0xC0040107L) // SystemStart失败
#define STATUS_MOU_CALLBACK_FAILED  ((NTSTATUS)0xC0040108L) // 鼠标回调注册失败
#define STATUS_KDB_CALLBACK_FAILED  ((NTSTATUS)0xC0040109L) // 键盘回调注册失败
#define STATUS_HV_PING_FAILED       ((NTSTATUS)0xC004010AL) // Hypervisor通讯失败
#define STATUS_REG_CALLBACK_FAILED      ((NTSTATUS)0xC004010BL) // 注册表回调注册失败
#define STATUS_SHUTDOWN_CALLBACK_FAILED   ((NTSTATUS)0xC004010CL) // 关机回调注册失败
#define STATUS_OB_CALLBACK_FAILED         ((NTSTATUS)0xC004010DL) // 进程保护回调注册失败
#define STATUS_ACCESS_DENIED              ((NTSTATUS)0xC0000022L) // 访问被拒绝
#define STATUS_FLT_INSTANCE_ALTITUDE_COLLISION ((NTSTATUS)0xC0000451L) // Ob 回调 Altitude 冲突

class PeaceDriverLoad
{
private:
    DWORD m_errorCode = 0;
public:
    PeaceDriverLoad();

    struct LoadDriverResult {
        bool success = false;
        wchar_t message[512] = {};  // 以 NUL 结尾，可当作 LPCWSTR 使用
    };

    //
    //得到加载驱动权限的令牌
    //
    BOOL enableDrvLoadPrivilege();

    //
    //加载驱动
    //
    BOOL installDriver(LPCWSTR ServiceName, LPCWSTR DriverPath);

    //
    //启动驱动 (SCM StartService)
    //
    BOOL startDriver(LPCWSTR ServiceName);

    //
    //启动驱动 (NtLoadDriver，直接返回 DriverEntry NTSTATUS)
    //
    BOOL startDriverNt(LPCWSTR ServiceName);

    //
    //停止驱动
    //
    BOOL stopDriver(LPCWSTR ServiceName);

    //
    //卸载驱动
    //
    BOOL unInstallDriver(LPCWSTR ServiceName);

    DWORD getErrorCode() const { return m_errorCode; }

    /*
    * 功能        自动加载PeaceDriver驱动
    *
    *
    * return      是否成功，原因说明
    */
    LoadDriverResult loadPeaceDriver();

    /*
    * 功能        自动卸载PeaceDriver驱动（停止服务、删除服务、清理 sys 文件）
    *
    * return      是否成功，原因说明
    */
    LoadDriverResult unloadPeaceDriver();

    BOOL driverIsLoaded();

    LPCWSTR NtLoadStatusToReason(DWORD status);
};

#endif // PEACEDRIVERLOAD_H
