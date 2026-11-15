#pragma once

#include <windows.h>

namespace Utils
{
	// bufferChars 含结尾 NUL；成功时 buffer 可作为 LPCWSTR 使用
	bool makeRandWords(int count, wchar_t* buffer, size_t bufferChars);

	bool makeRandName(wchar_t* buffer, size_t bufferChars);

	// directory 为输出目录（可带或不带末尾 \\）；fileName 不含 .sys 扩展名
	// message 可为 nullptr；失败时写入可读说明
	bool makeSysFile(LPCWSTR directory, LPCWSTR fileName, wchar_t* message, size_t messageChars);

	void display_logs();

	// 向控制台输出宽字符文本（避免 wprintf 在 GBK 控制台下乱码）
	void PrintLineW(LPCWSTR text);

	void PrintFormatW(LPCWSTR format, ...);

}
