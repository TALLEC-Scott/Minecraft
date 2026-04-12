set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Static-link the mingw runtime so the resulting .exe is self-contained —
# the user shouldn't need libgcc_s / libstdc++-6 / libwinpthread DLLs
# installed on the target machine.
set(CMAKE_C_FLAGS_INIT "-static-libgcc")
set(CMAKE_CXX_FLAGS_INIT "-static-libgcc -static-libstdc++")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")

# libstdc++.a references clock_gettime from winpthread, but the linker
# only makes one pass. Append winpthread to the implicit system-libs
# tail so it's seen *after* libstdc++ / libgcc_eh.
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "-lwinpthread")
set(CMAKE_C_STANDARD_LIBRARIES_INIT "-lwinpthread")
