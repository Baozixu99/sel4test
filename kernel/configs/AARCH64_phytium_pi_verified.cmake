#!/usr/bin/env -S cmake -P
#
# Copyright 2025, seL4 Project
#
# SPDX-License-Identifier: GPL-2.0-only
#

include(${CMAKE_CURRENT_LIST_DIR}/include/AARCH64_verified_include.cmake)

set(KernelPlatform "phytium-pi" CACHE STRING "")
