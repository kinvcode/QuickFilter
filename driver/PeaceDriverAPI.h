#pragma once

#include <cstdint>
#include <Windows.h>

namespace PeaceDriver {

// key used for executing hypercalls
inline constexpr uint64_t hypercall_key = 0xA7F39E2B4C81;

// signature that is returned by the ping hypercall
inline constexpr uint64_t hypervisor_signature = 'kinv';

struct logger_msg {
    static constexpr uint32_t max_msg_length = 128;

    // ID of the current message
    uint64_t id;

    // timestamp counter of the current message
    uint64_t tsc;

    // process ID of the VCPU that sent the message
    uint32_t aux;

    // null-terminated ascii string
    char data[max_msg_length];
};

// hypercall indices
enum hypercall_code : uint64_t {
    hypercall_ping = 0,
    hypercall_test,
    hypercall_unload,
    hypercall_read_phys_mem,
    hypercall_write_phys_mem,
    hypercall_read_virt_mem,
    hypercall_write_virt_mem,
    hypercall_query_process_cr3,
    hypercall_install_ept_hook,
    hypercall_remove_ept_hook,
    hypercall_flush_logs,
    hypercall_get_physical_address,
    hypercall_hide_physical_page,
    hypercall_unhide_physical_page,
    hypercall_get_hv_base,
    hypercall_install_mmr,
    hypercall_remove_mmr,
    hypercall_remove_all_mmrs,
    hypercall_install_shadow_page,
    hypercall_remove_shadow_page
};

// hypercall input
struct hypercall_input {
    // rax
    struct {
        hypercall_code code : 8;
        uint64_t       key : 56;
    };

    // rcx, rdx, r8, r9, r10, r11
    uint64_t args[6];
};

enum mmr_memory_mode {
    mmr_memory_mode_r = 0b001,
    mmr_memory_mode_w = 0b010,
    mmr_memory_mode_x = 0b100
};

/**
    * 模块：VT功能
    * 说明：该部分功能全部依赖于VT虚拟化技术，主要用于EPT HOOK
    *
    */

// VMCALL instruction, defined in hv.asm
uint64_t vmx_vmcall(hypercall_input& input);

// flush the hypervisor logs into a buffer
void flush_logs(uint32_t& count, logger_msg* msgs);

// ping the hypervisor to make sure it is running (returns hypervisor_signature)
uint64_t ping();

// VT功能开始

// check if the system is virtualized
bool is_hv_running();

// read from arbitrary physical memory
size_t read_phys_mem(void* dst, uint64_t src, size_t size);

// write to arbitrary physical memory
size_t write_phys_mem(uint64_t dst, void const* src, size_t size);

// read from virtual memory in another process
size_t read_virt_mem(uint64_t cr3, void* dst, void const* src, size_t size);

// write to virtual memory in another process
size_t write_virt_mem(uint64_t cr3, void* dst, void const* src, size_t size);

// get the kernel CR3 value of an arbitrary process
uint64_t query_process_cr3(uint64_t pid);

// install an EPT hook for the CURRENT logical processor ONLY
bool install_ept_hook(uint64_t orig_page_pfn, uint64_t exec_page_pfn);

// remove a previously installed EPT hook
void remove_ept_hook(uint64_t orig_page_pfn);

// translate a virtual address to its physical address
uint64_t get_physical_address(uint64_t cr3, void const* address);

// hide a physical page from the guest
bool hide_physical_page(uint64_t pfn);

// unhide a physical page from the guest
void unhide_physical_page(uint64_t pfn);

// get the base address of the hypervisor
void* get_hv_base();

// write to the logger whenever a certain physical memory range is accessed
void* install_mmr(uint64_t address, uint32_t size, uint8_t mode);

// remove an existing MMR
void remove_mmr(void* handle);

// remove every installed MMR
void remove_all_mmrs();

// VT功能结束

/**
    * 模块：注册表功能
    * 说明：注册表功能依赖于注册表通讯，通过写入注册表的方式调用驱动功能
    * 备注：非VT功能，尽量封装到注册表功能中
    */

// 注册表通讯测试
bool pingREG();

// 开启或关闭VT功能
DWORD enableVT(bool on);

//-------------------机器码--------------------------------------

bool virtualHard(bool enable);

//-------------------进程内存--------------------------------------

bool readMemory(ULONG64 processId, ULONG64 address, ULONG64 size, void* buffer);

bool writeMemory(ULONG64 processId, ULONG64 address, ULONG64 size, void* buffer);

ULONG64 allocMemory(ULONG64 processId, ULONG64 size);

bool freeMemory(ULONG64 processId, ULONG64 address);

bool queryMemory(ULONG64 processId, ULONG64 address, ULONG& meminfo);

//-------------------进程内存封装--------------------------------------

//读内存 字节型(BYTE)
BYTE readByte(ULONG pid, ULONG64 addr);

//读内存 短整数型(SHORT)
SHORT readShort(ULONG pid, ULONG64 addr);

//读内存 整数型(INT)
INT readInt(ULONG pid, ULONG64 addr);

//读内存 长整数型(INT64)
INT64 readInt64(ULONG pid, ULONG64 addr);

//读内存 浮点型(FLOAT)
FLOAT readFloat(ULONG pid, ULONG64 addr);

//读内存 字节集(Bytes)
BOOL readBytes(ULONG pid, ULONG64 addr, _Out_ PBYTE pDataBuffer, ULONG size);

//写内存 字节型(BYTE)
BOOL writeByte(ULONG pid, ULONG64 addr, _In_ BYTE pDataBuffer);

//写内存 短整数型(SHORT)
BOOL writeShort(ULONG pid, ULONG64 addr, _In_ SHORT pDataBuffer);

//写内存 整数型(INT)
BOOL writeInt(ULONG pid, ULONG64 addr, _In_ INT pDataBuffer);

//写内存 长整数型(INT64)
BOOL writeInt64(ULONG pid, ULONG64 addr, _In_ INT64 pDataBuffer);

//写内存 浮点型(FLOAT)
BOOL writeFloat(ULONG pid, ULONG64 addr, _In_ FLOAT pDataBuffer);

//写内存 字节集(Bytes)
BOOL writeBytes(ULONG pid, ULONG64 addr, _In_ PBYTE pDataBuffer, ULONG size);

//-------------------进程信息查询--------------------------------------

DWORD getPid(const wchar_t* processName);

ULONG64 getModuleBase(DWORD pid, const wchar_t* moduleName);

ULONG64 getModuleExport(DWORD pid, ULONG64 moduleBase, PCSTR funcName);

//-------------------注入功能--------------------------------------

bool remoteThread(ULONG64 processId, ULONG64 address);

bool injectionDll(ULONG64 processId, PCWSTR dllPath);

bool apcInject(ULONG64 processId, const void* dllBuffer, ULONG64 dllSize);

//-------------------进程保护--------------------------------------

bool protectPid(ULONG64 processId);

bool unprotectPid();

bool fakeProcess(ULONG64 targetProcessId, ULONG64 templateProcessId);

bool restoreFakeProcess();

bool protectWindow(DWORD processId, HWND hWnd);

bool unprotectWindow(DWORD processId, HWND hWnd);

bool antiSnapshot(HWND hWnd);

bool antiSnapshot(HWND hWnd, bool enable);



/**
    * 模块：注册表功能
    * 说明：注册表功能依赖于注册表通讯，通过写入注册表的方式调用驱动功能
    * 备注：非VT功能，尽量封装到注册表功能中
    */

//-------------------键鼠配置--------------------------------------

inline int resolutionWidth = 1920;
inline int resolutionHeight = 1080;
inline int keypressDelayMin = 50;
inline int keypressDelayMax = 80;

void setResolutionUsed(int width, int height);

int getResolutionWidth();

//-------------------键鼠--------------------------------------

VOID mouse(PBYTE pDataBuffer);

VOID keyboard(PBYTE pDataBuffer);

// 向当前线程所在会话的前台窗口发送 Unicode 字符串。
// status/extra 可空；失败时写入驱动返回的原因码
bool sendUnicodeStr(const wchar_t* text, unsigned long* status = nullptr, unsigned long* extra = nullptr);

//-------------------键鼠封装--------------------------------------

//-------------------------------鼠标--------------------------------------------------

// 左键单击
void mouseLeftClick();

// 左键双击
void mouseLeftDoubleClick();

// 左键按下
void mouseLeftDown();

// 左键弹起
void mouseLeftUp();

// 右键单击
void mouseRightClick();

// 右键按下
void mouseRightDown();

// 右键弹起
void mouseRightUp();

// 中键单击
void mouseMiddleClick();

// 中键按下
void mouseMiddleDown();

// 中键弹起
void mouseMiddleUp();

// 读取鼠标左右中按键状态（暂时无法通过驱动实现）, 1左键 2中键 3右键
// 返回 0弹起 1按下
int mouseKeyState(int keycode);

// 弹起所有鼠标按键
void mouseReleaseAll();

// 滚动鼠标滚轮 正数向下，负数向上
void mouseWheel(int number);

// 将鼠标移动到相对位置
void mouseMoveR(int x, int y);

// 将鼠标移动到绝对位置
void mouseMoveTo(int x, int y);

//-------------------------------键盘--------------------------------------------------

// 敲击指定按键, 可以指定按键次数times
void keyboardClick(int keycode, int times = 1);

// 按下指定按键
void keyboardDown(int keycode);

// 弹起指定按键
void keyboardUp(int keycode);

// 读取按键状态(暂时无法通过驱动实现)
// 返回 0弹起 1按下
int keyboardState(int keycode);

// 弹起所有键盘按键
void keyboardReleaseAll();
}
