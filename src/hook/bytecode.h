#pragma once

#include <vector>
#include <Windows.h>

inline std::vector<byte> operator+(std::vector<byte> a, std::vector<byte> b)
{
    for (int i = 0; i < b.size(); i++)
        a.push_back(b[i]);

    b.clear();
    return a;
}

inline std::vector<byte> intToBytes(int length)
{
    __int64 adr = (__int64)&length;
    std::vector<byte> c;
    for (size_t i = 0; i < 4; i++)
        c.push_back(*(byte*)adr++);
    return c;
}

inline std::vector<byte> longToBytes(long long length)
{
    __int64 adr = (__int64)&length;
    std::vector<byte> c;
    for (size_t i = 0; i < 8; i++)
        c.push_back(*(byte*)adr++);
    return c;
}

inline std::vector<byte> makeByteArray(std::vector<byte> data)
{
    return data;
}
