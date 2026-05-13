cmake_minimum_required(VERSION 3.16.0)

project(monkey-mnemosyne)

# 当作为子项目被 add_subdirectory 引入时，父项目已经完成了
# find_package 和环境设置，跳过以避免 seL4 库目标重复定义。
if(NOT TARGET sel4)
    find_package(musllibc REQUIRED)
    find_package(util_libs REQUIRED)
    find_package(seL4_libs REQUIRED)

    # Setup build environment
    musllibc_setup_build_environment_with_sel4runtime()
    sel4_import_libsel4()
    util_libs_import_libraries()
    sel4_libs_import_libraries()
endif()

# ----------------------------------------------------------------------------
# Sources
#
# All C / C++ sources under src/ form the reusable static library
# `monkey-mnemosyne-lib`.  The sole exception is `lab-main.cc`, which
# carries `int main()` and the lab-specific demo logic; it stays out of
# the library and is linked only into the standalone test executable.
# ----------------------------------------------------------------------------
file(GLOB_RECURSE lib_srcs src/*.c src/*.cc src/*.cpp)
list(REMOVE_ITEM lib_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/lab-main.cc)
# 排除本地 hyperamp_shm_queue.c，使用 front 版本避免与 hyperamp-server 链接时符号冲突
list(REMOVE_ITEM lib_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/monkey/net/hyperamp_shm_queue.c)
list(SORT lib_srcs)
list(REMOVE_DUPLICATES lib_srcs)

# ----------------------------------------------------------------------------
# C++ flags shared by the library and the standalone executable.
# Hoisted out of the original target_compile_options so that both targets
# stay binary-compatible (same RTTI/EH/ABI flags).
# ----------------------------------------------------------------------------
set(MNEMOSYNE_CXX_FLAGS
    -Wall
    -std=c++20
    -g
    -nostartfiles
    -fno-pic -fno-pie -no-pie
    -fno-rtti
    -fno-stack-protector
    -fno-threadsafe-statics
    -fno-exceptions
    -fno-use-cxa-atexit
    -fno-sized-deallocation
    -Wno-error=unused-function
    -fdiagnostics-color=always
)

# ----------------------------------------------------------------------------
# Reusable static library: monkey-mnemosyne-lib
# ----------------------------------------------------------------------------
add_library(monkey-mnemosyne-lib STATIC EXCLUDE_FROM_ALL ${lib_srcs})

target_include_directories(monkey-mnemosyne-lib
    PUBLIC
        # Pure-C facade header for downstream apps.
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        # Internal C++ sources.
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(monkey-mnemosyne-lib
    PUBLIC
        sel4_autoconf
        muslc
        sel4
        sel4runtime
        sel4utils
        sel4platsupport
        sel4muslcsys
        sel4allocman
    PRIVATE
        utils
)

# Apply the C++ flags to library sources only (they are .cc).
target_compile_options(monkey-mnemosyne-lib PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${MNEMOSYNE_CXX_FLAGS}>
)
target_compile_options(monkey-mnemosyne-lib PRIVATE -g)
set_property(TARGET monkey-mnemosyne-lib PROPERTY C_STANDARD 99)
