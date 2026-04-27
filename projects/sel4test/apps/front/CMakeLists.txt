cmake_minimum_required(VERSION 3.16.0)

project(front C)

find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)

# Setup build environment
musllibc_setup_build_environment_with_sel4runtime()
sel4_import_libsel4()
util_libs_import_libraries()
sel4_libs_import_libraries()

# Build front application
file(GLOB 
        deps 
        src/main.c
        src/common_utils.c
        src/frontend_proto.c
        src/session.c
        src/session_pool.c
        src/shared_mem_io.c
        src/hyperamp_shm_queue.c
        src/dev.c
        src/engine.c
        src/frontend_api.c
        src/senario_test.c
        unittest/*.c
)
# list(SORT deps)

add_executable(front EXCLUDE_FROM_ALL ${deps})

# Add include directories for front headers (本地 include 目录)
target_include_directories(front PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(front 
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
target_compile_options(front PRIVATE -g -O2)
set_property(TARGET front PROPERTY C_STANDARD 99)

# Set this image as the rootserver
include(rootserver)
DeclareRootserver(front)