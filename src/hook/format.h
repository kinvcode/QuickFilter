#pragma once

#include <QString>

// 64 位地址，固定 16 位十六进制并补零
inline QString formatAddress(quint64 address)
{
    return QStringLiteral("0x%1").arg(address, 16, 16, QChar('0')).toUpper();
}
