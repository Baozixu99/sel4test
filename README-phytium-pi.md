# seL4 移植到飞腾派 (Phytium Pi)

本项目基于 IMX8MP 配置，为飞腾派开发板创建了 seL4 微内核的移植。

## 硬件规格 (基于官方DTS)

- SoC: Phytium PE2204
- CPU: Phytium FTC310 (ARM v8 compatible)
- 架构: ARMv8-A AArch64
- 内存: 2GB DDR4 (预留启动地址0x80000000-0x80010000)
- 串口: ARM PL011 UART (UART1 @ 0x2800d000, 中断84)
- 中断控制器: GICv3 @ 0x30800000
- 定时器频率: 50MHz
- UART时钟: 48MHz

## 新增文件

### 内核配置
- `kernel/src/plat/phytium-pi/config.cmake` - 平台配置
- `kernel/src/plat/phytium-pi/overlay-phytium-pi.dts` - 设备树overlay
- `kernel/src/plat/phytium-pi/overlay-phytium-32bit.dts` - 32位模式overlay
- `kernel/tools/dts/phytium-pi.dts` - 主设备树文件
- `kernel/configs/AARCH64_phytium_pi_verified.cmake` - 验证配置

### API 定义
- `kernel/libsel4/sel4_plat_include/phytium-pi/sel4/plat/api/constants.h`

### 用户态支持库
- `projects/util_libs/libplatsupport/src/plat/phytium/chardev.c` - 字符设备
- `projects/util_libs/libplatsupport/src/plat/phytium/clock.c` - 时钟管理
- `projects/util_libs/libplatsupport/src/plat/phytium/serial.c` - 串口驱动 (PL011)
- `projects/util_libs/libplatsupport/plat_include/phytium/platsupport/plat/serial.h`
- `projects/util_libs/libplatsupport/plat_include/phytium/platsupport/plat/timer.h`
- `projects/util_libs/libplatsupport/plat_include/phytium/platsupport/plat/clock.h`

## 构建方法

**注意**: 确保在正确的目录中运行构建命令。构建命令应该在包含CMakeLists.txt的项目根目录中运行。

1. 使用提供的构建脚本:
   ```bash
   chmod +x build-phytium-pi.sh
   ./build-phytium-pi.sh
   ```

2. 或者手动构建:
   ```bash
   mkdir build-phytium-pi
   cd build-phytium-pi
   cmake -DCMAKE_TOOLCHAIN_FILE="../kernel/gcc.cmake" \
         -DPLATFORM="phytium-pi" \
         -DAARCH64=1 \
         -DKernelSel4Arch="aarch64" \
         -G Ninja \
         ..
   ninja
   ```

## 故障排除

如果遇到 "CMake Error: The source directory does not appear to contain CMakeLists.txt" 错误，请确保：

1. 您在项目根目录 (sel4test) 中运行构建命令
2. 确认存在顶级CMakeLists.txt文件
3. 如果在子目录中，使用 `cd ..` 返回到项目根目录

常见构建错误及解决方案：
- **缺少头文件错误**: 已通过添加机器特定头文件解决
- **时钟函数未定义**: 已简化时钟实现解决
- **串口初始化问题**: 已实现专用PL011驱动解决

## 注意事项

1. **硬件地址已更新**: 基于官方设备树的真实硬件地址：
   - UART1 基地址: 0x2800d000 (活跃的串口控制台)
   - GIC 基地址: 0x30800000
   - 内存起始地址: 0x80000000
   - UART1中断号: 84

2. **时钟频率**: 基于官方DTS的实际时钟频率:
   - ARM Generic Timer 频率: 50MHz
   - UART 时钟频率: 48MHz
   - CPU 频率: 待确认

3. **设备树**: 已使用飞腾派官方提供的设备树

4. **启动加载器**: 可能需要适配飞腾派的 bootloader

## 下一步工作

1. ✅ 获取飞腾派的详细硬件文档 (已完成)
2. ✅ 调整内存映射和外设地址 (已根据官方DTS更新)
3. ✅ 验证中断控制器配置 (已更新为GICv3@0x30800000)
4. 测试串口通信 (UART1@0x2800d000)
5. 添加其他外设支持 (Ethernet, GPIO, SPI, I2C等)
6. 优化性能参数

## 技术支持

如果需要进一步的移植支持，请提供:
1. 飞腾派的完整技术规格文档
2. 原厂提供的 Linux 设备树文件
3. U-Boot 或其他 bootloader 的配置信息
