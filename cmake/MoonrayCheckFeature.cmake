# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

include(CheckCXXSourceCompiles)

check_cxx_source_compiles([=[
    static_assert(__atomic_always_lock_free(sizeof(__int128), nullptr));

    int main()
    {
        return 0;
    }
]=] MOONRAY_HAS_LOCK_FREE_16BYTE_ATOMICS)

if(MOONRAY_HAS_LOCK_FREE_16BYTE_ATOMICS)
    message(STATUS "16-byte atomic operations are always lock-free")
else()
    message(STATUS "16-byte atomic operations require the MoonRay fallback")
endif()
