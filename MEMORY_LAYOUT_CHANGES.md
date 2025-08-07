# Phytium Pi 内存布局修改说明

## 设备树修改总结

### 1. 内存配置变化
**之前的配置：**
- 内存起始地址: `0x80000000` (2GB)
- 内存大小: 1GB+
- Load地址: `0x90100000`

**现在的配置：**
- 内存起始地址: `0xb0000000` (2.75GB)
- 内存大小: `0x30000000` (768MB)
- Load地址: `0xb0100000` (已更新)
- 内存结束地址: `0xe0000000` (3.5GB)

### 2. Reserved Memory (共享内存)
新增了共享内存区域：
```dts
reserved-memory {
    shm@e0000000 {
        no-map;
        reg = <0x00 0xde000000 0x00 0x00420000>;  // 4.125MB共享内存
    };
};
```

### 3. overlay-phytium-32bit.dts 文件作用

这个文件是**设备树覆盖文件**，专门用于32位ARM架构：

**主要功能：**
1. **架构特定配置**: 当编译32位版本时，CMake会自动包含这个overlay
2. **内存映射调整**: 32位系统可能需要不同的内存配置
3. **地址空间限制**: 32位ARM只有4GB地址空间，需要特殊处理

**工作原理：**
```cmake
# 在 kernel/src/plat/phytium-pi/config.cmake 中
if(KernelSel4ArchAarch32)
    list(APPEND KernelDTSList "src/plat/phytium-pi/overlay-phytium-32bit.dts")
endif()
```

当编译32位版本时，设备树编译器会将这个overlay合并到主DTS文件中，覆盖相应的节点。

### 4. 修改的文件列表

**已更新的配置文件：**
1. `kernel/src/plat/phytium-pi/overlay-phytium-pi.dts`
   - 更新 seL4,phys-base: `0x80200000` → `0xb0200000`

2. `kernel/src/plat/phytium-pi/overlay-phytium-32bit.dts`
   - 更新内存节点: `memory@80000000` → `memory@b0000000`
   - 更新内存大小: `0x40000000` → `0x30000000`

3. `kernel/src/plat/phytium-pi/config.cmake`
   - 更新CPU类型: `Cortex-A55` → `Cortex-A72`

4. `kernel/libsel4/sel4_plat_include/phytium-pi/sel4/plat/api/constants.h`
   - 更新架构常量: `cortex_a55.h` → `cortex_a72.h`

5. `tools/seL4/cmake-tool/helpers/application_settings.cmake`
   - 更新Load地址: `0x90100000` → `0xb0100000`

### 5. 内存布局图

```
地址范围                   用途
0x00000000 - 0xb0000000   系统保留区域
0xb0000000 - 0xb0100000   内核启动预留空间 (1MB)
0xb0100000 - 0xb0200000   ELF Loader区域 (1MB) 
0xb0200000 - ...          seL4内核代码段
...        - 0xde000000   用户空间可用内存
0xde000000 - 0xde420000   Reserved Memory (共享内存 4.125MB)
0xde420000 - 0xe0000000   剩余可用内存
```

### 6. 为什么需要这些修改？

1. **内存起始地址变化**: 硬件平台的内存映射发生了变化
2. **共享内存需求**: 新增的reserved memory用于与hypervisor或其他组件通信
3. **CPU核心简化**: 从4核简化为2核配置
4. **地址一致性**: 确保所有组件使用一致的内存地址映射

这些修改确保seL4能够在新的硬件配置下正确启动和运行。
