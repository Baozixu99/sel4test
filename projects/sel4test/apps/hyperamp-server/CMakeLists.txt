#
# HyperAMP 多通道统一服务器 for seL4
# 合并 hyperamp-server (CH0) 和 front (CH1) 为单一 rootserver
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.16.0)

project(hyperamp-server C)

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

# hyperamp-server 自身的源文件
set(HYPERAMP_SOURCES
    src/main.c
    src/channel.c
    src/channel_ch0.c
    src/channel_ch1.c
)

# front 项目的源文件（通过 symlink 引入，排除 front 自身的 main.c）
set(FRONT_SOURCES
    src/front_common_utils.c
    src/front_frontend_proto.c
    src/front_session.c
    src/front_session_pool.c
    src/front_shared_mem_io.c
    src/front_dev.c
    src/front_engine.c
    src/front_frontend_api.c
    src/front_senario_test.c
    src/front_hyperamp_shm_queue.c
)

add_executable(hyperamp-server EXCLUDE_FROM_ALL
    ${HYPERAMP_SOURCES}
    ${FRONT_SOURCES}
)

# Include 路径：front 的 include 在前，确保 front 的 hyperamp_shm_queue.h 被优先使用
target_include_directories(hyperamp-server PRIVATE
    ${FRONT_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
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
)

# Set executable properties 
target_compile_options(hyperamp-server PRIVATE -Werror -g -O2)

# front 源文件可能有格式化警告，对其禁用 -Werror 避免编译失败
foreach(front_src ${FRONT_SOURCES})
    set_source_files_properties(${front_src} PROPERTIES COMPILE_OPTIONS "-Wno-error;-g;-O2")
endforeach()

# channel_ch1.c 包含了 front 头文件，也需要禁用 -Werror
set_source_files_properties(src/channel_ch1.c PROPERTIES COMPILE_OPTIONS "-Wno-error;-g;-O2")
set_property(TARGET hyperamp-server PROPERTY C_STANDARD 99)

# Set this image as the rootserver
include(rootserver)
DeclareRootserver(hyperamp-server)
