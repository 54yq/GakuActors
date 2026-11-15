#pragma once
#include <stdint.h>

namespace Il2cppUtils {
    // IL2CPP System.ReadOnlySpan<byte>: ByReference<byte> followed by Int32 length.
    // Pass the complete value so ARM64 and Windows x64 use their native aggregate ABI.
    struct ReadOnlySpanByte {
        const uint8_t* data;
        int32_t length;
    };

    static_assert(sizeof(ReadOnlySpanByte) == 16);
}
