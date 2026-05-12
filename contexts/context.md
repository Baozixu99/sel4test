# HyperAMP 多通道跨虚拟机通信系统 — 工程上下文 (Agent Handoff Prompt)

> **致接手的大模型 / Agent：**
> 欢迎来到 HyperAMP 系统。这是一个运行中的、跨越 Type-1 Hypervisor (hvisor) + seL4 + Linux 的真实工程系统。
> 本文档是该系统的**核心工程沉淀**。你看到的诸多设计（如双通道隔离、轮询模式、Cache 手动维护）均为无数次踩坑与权衡后的最优解。
> **最高指令：不要将系统理想化重构为“教科书架构”。你的首要任务是在现有工程约束下解决问题并扩展功能。**

---

## 一、系统操作铁律 (Agent Directives)

### 🔴 绝对禁止 (Never Do)
1. **不要打破多通道协议隔离**：CH0 (`channel_ch0.c`) 只处理加解密、验签及 Bulk 数据处理；CH1 (`channel_ch1.c`) 只负责网络代理代理栈封装；CH2 为预留通道，专用于远程内存访问 (Remote Memory Access)。各通道消息域绝不可混用，跨通道流转必须通过明确的接口桥接。
2. **不要擅自修改共享内存物理地址布局**：底层 Hypervisor 强绑定了特定的 PA (Physical Address)，任何偏移量或边界的修改会导致立刻崩溃。
3. **不要引入基于中断 (Interrupt) 或信号量 (Semaphore) 的 IPC 模型**：目前极简的并发 Polling（轮询）模式比中断更稳定，且避免了复杂的虚实中断路由问题。
4. **不要删除或合并 Cache 维护逻辑**：所有的 `hyperamp_cache_invalidate` 与 `hyperamp_cache_clean` 都是跨 VM 数据一致性的生命线。
5. **严禁混淆 Creator 与 Connector**：同一个 Channel 有且只能有一个 Creator 负责格式化队列（目前全是 seL4 充当 Creator）。

### 🟢 必须遵守 (Always Do)
1. **尊重定长块 (Fixed Block Size)**：`HyperampShmQueue` 的 `block_size` 严格绑定为 4096 字节。超长数据必须进行 Bulk 分块逻辑处理。
2. **遵守 seL4 的类型约定**：seL4 用户态必须使用 `seL4_Word`（而非内核态的 `word_t`），且 IPC 寄存器读取必须优先调用 `seL4_GetMR()`。
3. **保持包含路径的优先级**：CMakeLists 中 `front/include` 永远优先于 `hyperamp-server/include`，以确保全局使用新版的 `hyperamp_shm_queue.h`。

---

## 二、架构演化与系统现状

本系统从**单通道雏形**演化为如今的**多通道分离 + 跨通道桥接**架构。
**演化原因**：早期数据面与控制面耦合，导致加解密请求与网络代理握手产生时序冲突。分离 CH0 和 CH1 彻底解耦了内部可信处理与外部网络分发。

### 系统组件全景图
```text
┌─────────────────────────────────────────────────────┐
│                   hvisor (EL2)                      │
│         Type-1 Hypervisor / VM 管理 / IVC           │
├───────────────────────┬─────────────────────────────┤
│     Linux Zone        │        seL4 Zone            │
│                       │                             │
│  hvisor-tool          │  hyperamp-server            │
│  (CH0 connector)      │  (统一 rootserver)          │
│       ↕ CH0           │    ├── channel_ch0 (处理)   │
│                       │    ├── channel_ch1 (网代)   │
│  HighSpeedCProxy      │    ├── channel_ch2 (远存)   │
│  (CH1 connector)      │    └── main.c (多通道轮询)  │
│       ↕ CH1, CH2      │                             │
├───────────────────────┴─────────────────────────────┤
│          共享内存 (0x7E000000 - 0x7E3FFFFF)         │
└─────────────────────────────────────────────────────┘
```

**组件状态**：
1. **hvisor (EL2)**：提供硬件虚拟化、物理内存映射。当前版本不依赖中断。
2. **hvisor-tool (Linux)**：CH0 的 Connector 端，负责发出图像/文本的加解密请求。
3. **hyperamp-server (seL4)**：统一的后端。将原有的 `front` 源码库合并入编译系统（直接通过源码包含），在 `main.c` 中执行并发轮询。
4. **HighSpeedCProxy (Linux)**：CH1 的 Connector 端，负责真正将 seL4 的网络代理包发往真实网卡。
5. **预留 CH2**：设计用于未来的**远程内存访问 (Remote Memory Access)**，物理地址空间已在底层分配，供后续直接进行跨 VM 的内存级存取。

---

## 三、核心机制：数据流与跨通道桥接 (Cross-Channel Bridging)

跨通道桥接是系统的**核心亮点**。它实现了安全处理（CH0）与对外网络分发（CH1）的协同工作。

### 数据流模型
1. **独立的 CH0 请求/响应**（如普通解密/验签）：
   Linux (hvisor-tool) ─CH0→ seL4 处理完成 ─CH0→ Linux (hvisor-tool)
2. **独立的 CH1 代理流**：
   seL4 (front engine) ─CH1→ Linux (cproxy) → 真实网络
3. **CH0 → CH1 跨通道桥接 (加密/验证+对外分发)**：
   Linux 将图像发入 CH0，seL4 完成加密处理后，不仅通过 CH0 回复已完成，还会将数据通过 CH1 分发到网络。
   ```text
   Linux (hvisor-tool) ──CH0──> seL4 (加解密/验签)
                                      │ (仅针对加密服务)
                                      ├── CH1 ──> Linux (cproxy) ──> 外部网络
                                      └── CH0 ──> Linux (hvisor-tool)
   ```

**工程实现限制**：
`channel_ch0.c` 调用 `ch0_try_forward_bulk_to_ch1` 后，数据只放入了 CH1 内部的 `F2B (Front-to-Backend)` 队列。真实写入共享内存的操作是在下一次主循环执行 `ch1_process_message()` 中的引擎运转时发生的。

---

## 四、共享内存与物理约定

### 4.1 物理地址与通道容量 (千万不可更改)
| 通道 | 物理基址 | 大小 | TX Queue (seL4方向) | RX Queue (seL4方向) | Data 区域 | 槽位 (Capacity) |
|------|----------|------|--------------------|--------------------|-----------|-----------------|
| CH0 | `0x7E000000` | 2MB | `0x7E000000` | `0x7E001000` | `0x7E002000` | 256 |
| CH1 | `0x7E200000` | 1MB | `0x7E200000` | `0x7E201000` | `0x7E202000` | 253 |
| CH2 | `0x7E300000` | 1MB | `0x7E300000` | `0x7E301000` | `0x7E302000` | 预留 |

*注意：TX/RX 的视角是以 seL4 为主的。对于 Linux 来说方向正好相反。*
地址在内核态 `boot.c` 初始化，并通过 IPC buffer 的 `msg[2..10]` 传递给 rootserver。如果 IPC buffer 被污染（例如在提取地址前错误地执行了其他 syscall），内存映射会立即崩溃。

### 4.2 Creator/Connector 身份逻辑
为避免两个 VM 启动时死锁或竞态条件，系统强制规范身份：
- **Creator (seL4 侧)**：负责 `magic` 初始化、`head/tail` 重置。
- **Connector (Linux 侧)**：在 `hyperamp_linux_init` 时第二个参数传 `0`。只负责检测 `magic` 是否就绪，绝不清理队列。

---

## 五、隐性工程知识与历史经验池 (Implicit Knowledge)

这是大量排雷过程留下的“血泪教训”，任何修改前必须参考：

### 1. 缓存一致性迷雾 (Cache Coherency)
**现象**：偶尔发生数据包截断、脏数据、头端乱码。
**真相**：Linux 写入的数据停在 L1/L2 Cache 未刷入主存，或 seL4 直接读取了脏 Cache。
**教训**：**绝对不要**绕过 `channel.c` 中提供的 `shm_read_buffer()` 和 `shm_write_buffer()`。手动从 Data Region 读写数据前后，必须严格执行 `hyperamp_cache_invalidate` 与 `hyperamp_cache_clean`。

### 2. Bulk 内存耗尽与引擎饥饿
**历史**：过去在发送 1MB 大图时，seL4 经常发生 `insufficient memory resource` 错误并导致系统超时。
**原因**：Bulk 数据被切分成几百个 4096 字节的块（Segment）并挤入 `F2B` 队列。此时 seL4 的堆内存被瞬间抽干。
**修复约定**：在 `channel_ch0.c` 的转发死循环中，现在每成功入队一次或遇到压力时，都强制执行一遍 `frontend_engine_run_hyperamp_once()`，以促使引擎立即刷出内存，保证大文件传输时堆内存始终维持低水位。不要删除这个强制唤醒。

### 3. 返回值 0 (`HYPERAMP_OK`) 陷阱
**历史**：`ch1_forward_bulk_raw_data` 曾返回 0 表示发送成功，导致外层 `pos += ret` 彻底陷入无限循环 (0 递增)。
**修复约定**：转发接口（非协议 Header 接口）必须透传底层成功发送的真实字节数。

### 4. 网络代理的首次超时 (First-time Session Overhead)
**现象**：`hyperamp_linux` 第一次执行会抛出 `Timeout waiting for Bulk response`，但文件依然能生成，第二次之后全部正常。
**原因**：首次通信需要 CH1 通过 `CREATE` 和 `RESP` 包与 `receiver.py` 建立 Session，这个 UDP 握手的延迟导致总体时间微超出客户端内硬编码的 5 秒。
**教训**：这是正常的架构开销。

---

## 六、构建指南与代码导航 (Agent Quickstart)

### 编译流水线
```bash
cd sel4test
rm -rf cbuild && mkdir cbuild && cd cbuild
../init-build.sh -DPLATFORM=imx8mp-evk -DAARCH64=1 -DSel4testApp=hyperamp-server
ninja
```

### 代码阅读链路 (推荐顺序)
1. **基石层**：`front/include/hyperamp_shm_queue.h` 和 `front/src/hyperamp_shm_queue.c` (队列核心)
2. **入口层**：`hyperamp-server/src/main.c` (IPC读取，双通道初始化，大轮询)
3. **协议层**：`hyperamp-server/src/channel.c` (含安全的 Cache 维护封装)
4. **业务层 CH0**：`hyperamp-server/src/channel_ch0.c` (加解密服务与 `ch0_try_forward_bulk_to_ch1` 桥接点)
5. **业务层 CH1**：`hyperamp-server/src/channel_ch1.c` (向 `cproxy` 对接的网络前置栈)

> **Agent 确认指令**：当你接到这个项目的维护任务时，请优先确认你理解了 Cache 一致性和 Creator/Connector 的隔离原则。祝你好运。
