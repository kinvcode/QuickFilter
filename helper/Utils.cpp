#include "Utils.h"

#include <stdio.h>
#include <cstdarg>
#include <ctime>
#include <cstring>
#include <string>
#include <vector>
#include <sddl.h>
#include "driver/peacesys.h"

namespace Utils
{
	namespace
	{
		using PFN_RtlRandomEx = ULONG(WINAPI*)(PULONG Seed);

		PFN_RtlRandomEx GetRtlRandomEx()
		{
			static PFN_RtlRandomEx fn = reinterpret_cast<PFN_RtlRandomEx>(
				GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlRandomEx"));
			return fn;
		}

		ULONG NextRandom(ULONG* seed)
		{
			if (PFN_RtlRandomEx const rtlRandomEx = GetRtlRandomEx())
				return rtlRandomEx(seed);

			*seed = (*seed * 1103515245u + 12345u) & 0x7fffffffu;
			return *seed;
		}

		ULONG CreateRandomSeed()
		{
			return GetTickCount() ^ GetCurrentProcessId();
		}

		wchar_t ToggleWordCase(wchar_t ch, int index)
		{
			if (index % 2 == 0)
			{
				if (ch >= L'a' && ch <= L'z')
					return static_cast<wchar_t>(ch - 32);
			}
			else if (ch >= L'A' && ch <= L'Z')
			{
				return static_cast<wchar_t>(ch + 32);
			}
			return ch;
		}


		bool BuildSysFilePath(
			LPCWSTR directory,
			LPCWSTR fileName,
			wchar_t* filePath,
			size_t filePathChars)
		{
			if (!directory || !fileName || !filePath || filePathChars == 0)
				return false;

			const size_t dirLen = wcslen(directory);
			if (dirLen == 0 || fileName[0] == L'\0')
				return false;

			if (directory[dirLen - 1] == L'\\')
				return _snwprintf_s(filePath, filePathChars, _TRUNCATE, L"%s%s.sys", directory, fileName) > 0;

			return _snwprintf_s(filePath, filePathChars, _TRUNCATE, L"%s\\%s.sys", directory, fileName) > 0;
		}

        void SetMessage(wchar_t* message, size_t messageChars, LPCWSTR text)
        {
            if (!message || messageChars == 0)
                return;
            message[0] = L'\0';
            if (text)
                wcsncpy_s(message, messageChars, text, _TRUNCATE);
        }

        void SetMessageFormat(wchar_t* message, size_t messageChars, LPCWSTR format, ...)
        {
            if (!message || messageChars == 0 || !format)
                return;
            va_list args;
            va_start(args, format);
            _vsnwprintf_s(message, messageChars, _TRUNCATE, format, args);
            va_end(args);
        }
	}

	bool makeRandWords(int count, wchar_t* buffer, size_t bufferChars)
	{
		if (!buffer || bufferChars == 0)
			return false;

		buffer[0] = L'\0';
		if (count <= 0)
			return true;

		if (static_cast<size_t>(count) >= bufferChars)
			return false;

		static const wchar_t worlds[] =
			L"QWERTYUIPPASDDFGFDGXZCBVCBZXasdqwdasdczxcvvbdfgrtuuyklui";
		const size_t worldsLen = wcslen(worlds);

		if (worldsLen == 0)
			return false;

		ULONG seed = CreateRandomSeed();
		for (int i = 0; i < count; ++i)
		{
			const size_t position = NextRandom(&seed) % worldsLen;
			buffer[i] = ToggleWordCase(worlds[position], i);
		}
		buffer[count] = L'\0';
		return true;
	}

	bool makeRandName(wchar_t* buffer, size_t bufferChars)
	{
		if (!buffer || bufferChars == 0)
			return false;

		SYSTEMTIME localTime = {};
		GetLocalTime(&localTime);

		const int value =
			(localTime.wSecond + localTime.wYear) * static_cast<int>(localTime.wMonth);

		wchar_t words[16] = {};
		if (!makeRandWords(5, words, _countof(words)))
			return false;

		return _snwprintf_s(buffer, bufferChars, _TRUNCATE, L"%d%s", value, words) > 0;
	}

	bool makeSysFile(LPCWSTR directory, LPCWSTR fileName, wchar_t* message, size_t messageChars)
	{
		SetMessage(message, messageChars, nullptr);

		if (!directory || directory[0] == L'\0' || !fileName || fileName[0] == L'\0')
		{
			SetMessage(message, messageChars, L"释放驱动文件失败：目录或文件名为空");
			return false;
		}

		wchar_t filePath[MAX_PATH] = {};
		if (!BuildSysFilePath(directory, fileName, filePath, _countof(filePath)))
		{
			SetMessage(message, messageChars, L"释放驱动文件失败：目标路径过长");
			return false;
		}

		HANDLE const file = CreateFileW(
			filePath,
			GENERIC_WRITE,
			0,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			SetMessageFormat(message, messageChars,
				L"释放驱动文件失败：CreateFile 失败 (%s)，错误码 %lu",
				filePath, GetLastError());
			return false;
		}

		DWORD written = 0;
		const BOOL writeOk = WriteFile(
			file,
			FileBuffer::SysData,
			static_cast<DWORD>(sizeof(FileBuffer::SysData)),
			&written,
			nullptr);
		CloseHandle(file);

		if (!writeOk || written != sizeof(FileBuffer::SysData))
		{
			DeleteFileW(filePath);
			SetMessageFormat(message, messageChars,
				L"释放驱动文件失败：WriteFile 失败 (%s)，错误码 %lu",
				filePath, GetLastError());
			return false;
		}

		SetMessageFormat(message, messageChars, L"驱动文件已释放：%s", filePath);
		return true;
	}

	void display_logs()
	{
	}

	void PrintLineW(LPCWSTR text)
	{
		if (!text)
			return;

		HANDLE const hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		if (hOut == INVALID_HANDLE_VALUE)
			return;

		const DWORD length = static_cast<DWORD>(wcslen(text));
		if (length == 0)
			return;

		DWORD written = 0;
		WriteConsoleW(hOut, text, length, &written, nullptr);
		WriteConsoleW(hOut, L"\r\n", 2, &written, nullptr);
	}

	void PrintFormatW(LPCWSTR format, ...)
	{
		if (!format)
			return;

		wchar_t buffer[512] = {};
		va_list args;
		va_start(args, format);
		_vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
		va_end(args);
		PrintLineW(buffer);
	}

}
