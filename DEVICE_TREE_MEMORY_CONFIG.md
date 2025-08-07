# seL4 设备树 Memory 配置详解

## 两种Memory配置的区别

### 1. Hardware Memory Description (硬件内存描述)
```dts
// overlay-phytium-32bit.dts
memory@b0000000 {
    device_type = "memory";
    reg = <0x00 0xb0000000 0x00 0x30000000>;  // 物理内存: 起始+大小
};
```

**用途**:
- 描述物理内存硬件布局
- 告诉内核实际可用的RAM位置和大小
- 用于内存管理子系统初始化
- 32位/64位可能需要不同配置

### 2. seL4 Kernel Load Address (内核加载地址)
```dts
// overlay-phytium-pi.dts  
memory {
    #address-cells = <2>;
    #size-cells = <2>;
    seL4,phys-base = <0x0 0xb0200000>;  // 内核加载基地址
};
```

**用途**:
- seL4特有属性，指定内核代码加载位置
- 必须在物理内存范围内
- 通常比内存起始地址高，预留bootloader空间
- 影响内核镜像的链接地址

## Reserved Memory 的处理

### 主设备树 (phytium-pi.dts)
```dts
reserved-memory {
    shm@e0000000 {
        no-map;
        reg = <0x00 0xde000000 0x00 0x00420000>;
    };
};
```

### Overlay文件中的对应
```dts
// overlay-phytium-pi.dts (64位)
reserved-memory {
    hvisor_shm: shm@de000000 {
        compatible = "shared-dma-pool";
        no-map;
        reg = <0x0 0xde000000 0x0 0x420000>;
    };
};

// overlay-phytium-32bit.dts (32位)  
reserved-memory {
    hvisor_shm_32: shm@de000000 {
        compatible = "shared-dma-pool";
        no-map;
        reg = <0x00 0xde000000 0x00 0x420000>;
    };
};
```

**为什么需要在overlay中重复定义？**
1. **设备树合并机制**: overlay会与主DTS合并，确保完整性
2. **架构特定配置**: 32位和64位可能需要不同的reserved memory配置
3. **seL4内核感知**: 让seL4内核知道哪些内存区域不可使用

## 完整的内存布局

```
地址范围                  用途                          配置文件
-------------------------------------------------------------------
0xb0000000               物理内存起始                   memory@b0000000
0xb0000000-0xb0100000    ELF Loader预留 (1MB)          -
0xb0100000-0xb0200000    其他组件预留 (1MB)             -  
0xb0200000               seL4内核加载基地址             seL4,phys-base
0xb0200000-0xde000000    seL4可用内存 (~700MB)         -
0xde000000-0xde420000    Reserved Memory (4.125MB)     reserved-memory
0xde420000-0xe0000000    剩余可用内存 (~30MB)          -
0xe0000000               物理内存结束                   -
```

## 设备树编译流程

1. **主设备树**: `phytium-pi.dts` (完整硬件描述)
2. **通用overlay**: `overlay-phytium-pi.dts` (seL4特定配置)
3. **架构overlay**: `overlay-phytium-32bit.dts` (32位特定配置)

编译时CMake会根据架构选择合适的overlay进行合并：
```cmake
# 64位模式
list(APPEND KernelDTSList "tools/dts/${KernelPlatform}.dts")
list(APPEND KernelDTSList "src/plat/phytium-pi/overlay-${KernelPlatform}.dts")

# 32位模式 (额外添加)
if(KernelSel4ArchAarch32)
    list(APPEND KernelDTSList "src/plat/phytium-pi/overlay-phytium-32bit.dts")
endif()
```

## 关键点总结

1. **memory@xxx** = 物理内存硬件描述
2. **seL4,phys-base** = seL4内核加载地址
3. **reserved-memory** = 禁止内核使用的内存区域
4. **overlay文件** = 架构/内核特定的配置覆盖

这样的设计确保了灵活性和正确性！
