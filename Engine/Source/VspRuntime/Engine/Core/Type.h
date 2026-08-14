#pragma once

#include <cstdint>

using Byte = uint8_t;

#if VSP_PLATFORM_WINDOWS
using Char_t = wchar_t;
#else
using Char_t = int8_t;
#endif
