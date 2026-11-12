#pragma once
#include <QtGlobal>

namespace rs::editorlimits {
inline constexpr int defaultMiB = 5;
inline constexpr int maximumMiB = 256;
inline constexpr quint64 MiB = 1024 * 1024;
int sizeMiB();
quint64 sizeBytes();
}
