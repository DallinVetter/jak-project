# CMake toolchain for the native PS5 build (see ps5/README.md).
#
# The compiler is the boilerplate's prospero-clang18 wrapper, reached through the
# launcher script ps5/build_gk.sh writes. CMake only compiles and archives here;
# the title is linked, converted and signed by ps5/stage_title.py.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(NOT PS5_COMPILER_LAUNCHER)
    message(FATAL_ERROR "PS5_COMPILER_LAUNCHER is not set: configure through ps5/build_gk.sh")
endif()
set(CMAKE_C_COMPILER "${PS5_COMPILER_LAUNCHER}")
set(CMAKE_CXX_COMPILER "${PS5_COMPILER_LAUNCHER}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PS5_COMPILER_LAUNCHER)

set(CMAKE_C_FLAGS_INIT "-fPIC -ffunction-sections -fdata-sections")
# The PS5 target turns RTTI off by default; OpenGOAL uses dynamic_cast and typeid.
set(CMAKE_CXX_FLAGS_INIT "-fPIC -ffunction-sections -fdata-sections -frtti")
set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)

set(PS5 ON CACHE BOOL "Native PS5 build" FORCE)
set(STATICALLY_LINK ON CACHE BOOL "" FORCE)
