#
# Copyright 2025, seL4 Project
#
# SPDX-License-Identifier: GPL-2.0-only
#

declare_platform(phytium-pi KernelPlatformPhytiumPi PLAT_PHYTIUM_PI KernelArchARM)

if(KernelPlatformPhytiumPi)
    declare_seL4_arch(aarch64 aarch32)
    # Phytium-Pi uses PE2204 SoC with FTC310 cores
    set(KernelArmCortexA55 ON)  # FTC310 is compatible with Cortex-A55
    set(KernelArchArmV8a ON)
    set(KernelArmGicV3 ON)
    # Enable user access to generic timer for ltimer support
    set(KernelArmExportPCNTUser ON CACHE BOOL "" FORCE)
    config_set(KernelARMPlatform ARM_PLAT ${KernelPlatform})
    set(KernelArmMach "phytium" CACHE INTERNAL "")
    list(APPEND KernelDTSList "tools/dts/${KernelPlatform}.dts")
    list(APPEND KernelDTSList "src/plat/phytium-pi/overlay-${KernelPlatform}.dts")
    if(KernelSel4ArchAarch32)
        list(APPEND KernelDTSList "src/plat/phytium-pi/overlay-phytium-32bit.dts")
    endif()
    declare_default_headers(
        TIMER_FREQUENCY 50000000
        MAX_IRQ 256
        TIMER drivers/timer/arm_generic.h
        INTERRUPT_CONTROLLER arch/machine/gic_v3.h
        NUM_PPI 32
        CLK_MAGIC 1llu
        CLK_SHIFT 3u
        KERNEL_WCET 10u
    )
endif()

add_sources(
    DEP "KernelPlatformPhytiumPi"
    CFILES src/arch/arm/machine/gic_v3.c src/arch/arm/machine/l2c_nop.c
)
