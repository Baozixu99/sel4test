#
# HyperAMP 多通道统一服务器 for seL4
# 合并 hyperamp-server (CH0) + front (CH1) + monkey-mnemosyne (CH2) 为单一 rootserver
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.16.0)

project(hyperamp-server C CXX)

find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)

# Setup build environment
musllibc_setup_build_environment_with_sel4runtime()
sel4_import_libsel4()
util_libs_import_libraries()
sel4_libs_import_libraries()

# 获取 front 项目的路径
set(FRONT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../front)

# 获取 monkey-mnemosyne 项目路径并作为子目录引入（构建 monkey-mnemosyne-lib 静态库）
set(MNEMOSYNE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../monkey-mnemosyne)
add_subdirectory(${MNEMOSYNE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/monkey-mnemosyne)

# hyperamp-server 自身的源文件
set(HYPERAMP_SOURCES
    src/main.c
    src/channel.c
    src/channel_ch0.c
    src/channel_ch1.c
    src/channel_ch2.c
)

# front 项目的源文件（直接引用原始路径，排除 front 自身的 main.c）
set(FRONT_SOURCES
    ${FRONT_DIR}/src/common_utils.c
    ${FRONT_DIR}/src/frontend_proto.c
    ${FRONT_DIR}/src/session.c
    ${FRONT_DIR}/src/session_pool.c
    ${FRONT_DIR}/src/shared_mem_io.c
    ${FRONT_DIR}/src/dev.c
    ${FRONT_DIR}/src/engine.c
    ${FRONT_DIR}/src/frontend_api.c
    ${FRONT_DIR}/src/senario_test.c
    ${FRONT_DIR}/src/hyperamp_shm_queue.c
)

add_executable(hyperamp-server EXCLUDE_FROM_ALL
    ${HYPERAMP_SOURCES}
    ${FRONT_SOURCES}
)

# Include 路径：front 的 include 在前，确保 front 的 hyperamp_shm_queue.h 被优先使用
target_include_directories(hyperamp-server PRIVATE
    ${FRONT_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MNEMOSYNE_DIR}/include
)

target_link_libraries(hyperamp-server 
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
        monkey-mnemosyne-lib
)

# Set executable properties 
target_compile_options(hyperamp-server PRIVATE -Werror -g -O2)

# front 源文件可能有格式化警告，对其禁用 -Werror 避免编译失败
foreach(front_src ${FRONT_SOURCES})
    set_source_files_properties(${front_src} PROPERTIES COMPILE_OPTIONS "-Wno-error;-g;-O2")
endforeach()

# channel_ch1.c 和 channel_ch2.c 包含了外部头文件，也需要禁用 -Werror
set_source_files_properties(src/channel_ch1.c PROPERTIES COMPILE_OPTIONS "-Wno-error;-g;-O2")
set_source_files_properties(src/channel_ch2.c PROPERTIES COMPILE_OPTIONS "-Wno-error;-g;-O2")
set_property(TARGET hyperamp-server PROPERTY C_STANDARD 99)

# Set this image as the rootserver
include(rootserver)
DeclareRootserver(hyperamp-server)
