# HyperAMP 多通道跨虚拟机通信系统 — 工程上下文 (Agent Handoff Prompt)

> **致接手的大模型 / Agent：**
> 这是一个运行中的、跨越 Type-1 Hypervisor (hvisor) + seL4 + Linux 的真实工程系统。
> 本文档是该系统的**核心工程沉淀**。你看到的诸多设计（如多通道隔离、轮询模式、Cache 手动维护）均为当前硬件约束与工程阶段下验证通过的稳定方案。未经充分验证前，不要推翻这些设计。
> **最高指令：不要将系统理想化重构为"教科书架构"。你的首要任务是在现有工程约束下解决问题并扩展功能。**

> **版本锚定**：本文档对应 `HighSpeedCProxy` 分支。若物理地址布局、IPC buffer 偏移、通道容量等发生变化，必须同步更新本文档第四章的表格。修改 `boot.c` 中的地址映射时，必须同步检查本文档是否需要更新。

---

## 一、系统现状快照

**已验证、稳定运行的功能**：
- CH0/CH1/CH2 三通道共享内存通信（polling 模式，三通道并发轮询）
- CH0 加解密 / 签名验证 / 字段校验 / Bulk 大文件处理
- CH1 网络代理通信（Session 建立 + 数据转发）
- CH2 远程内存访问（monkey-mnemosyne，TCP 协议栈经 HyperAMP 透传）
  - 主动连接 Monkey Mnemosyne 服务端（TCP，默认端口 10100）
  - Session 持久化：首次轮询时建立连接，后续轮询复用同一 Session
  - plainText 数据交换 + checksum 校验已验证通过
  - 4KiB 共享页 tryAlloc/readBlock/writeBlock 内存操作可用
- CH0 → CH1 跨通道桥接：加密和解密结果均可通过网络代理分发
- Bulk 数据分块传输 + 引擎强制流转内存回收（支持 ~2MB 以内的文件）
- Linux 侧 receiver.py 接收并落盘转发数据（.png 格式）
- HyperAMP polling 通信链路稳定运行

**未实现 / 规划中**：
- 中断驱动通信模式（当前全部使用 polling）
- CH2 的 Linux 侧 Connector（cproxy-B）尚未独立部署，当前 CH2 通过 seL4 内嵌的 HyperAmpBridge 直接驱动
- seL4 端缺页处理 + 页面管理（monkey-mnemosyne 的设计目标，尚未实现）

---

## 二、系统操作铁律 (Agent Directives)

### [NEVER] 绝对禁止
1. **不要打破多通道协议隔离**：CH0 (`channel_ch0.c`) 只处理加解密、验签及 Bulk 数据处理；CH1 (`channel_ch1.c`) 只负责网络代理协议栈封装；CH2 (`channel_ch2.c`) 专用于远程内存访问 (Remote Memory Access)，通过 monkey-mnemosyne C++ 库实现。各通道消息域绝不可混用，跨通道流转必须通过明确的接口桥接。
2. **不要擅自修改共享内存物理地址布局**：底层 Hypervisor 强绑定了特定的 PA (Physical Address)，任何偏移量或边界的修改会导致立刻崩溃。
3. **不要引入基于中断 (Interrupt) 或信号量 (Semaphore) 的 IPC 模型**：目前极简的并发 Polling（轮询）模式比中断更稳定，且避免了复杂的虚实中断路由问题。
4. **不要删除或合并 Cache 维护逻辑**：所有的 `hyperamp_cache_invalidate` 与 `hyperamp_cache_clean` 都是跨 VM 数据一致性的生命线。
5. **严禁混淆 Creator 与 Connector**：同一个 Channel 有且只能有一个 Creator 负责格式化队列（目前全是 seL4 充当 Creator）。

### [ALWAYS] 必须遵守
1. **尊重定长块 (Fixed Block Size)**：`HyperampShmQueue` 的 `block_size` 严格绑定为 4096 字节。超长数据必须进行 Bulk 分块逻辑处理。
2. **遵守 seL4 的类型约定**：seL4 用户态必须使用 `seL4_Word`（而非内核态的 `word_t`），且 IPC 寄存器读取必须优先调用 `seL4_GetMR()`。
3. **保持包含路径的优先级**：CMakeLists 中 `front/include` 永远优先于 `hyperamp-server/include`，以确保全局使用新版的 `hyperamp_shm_queue.h`。
4. **monkey-mnemosyne 的 hyperamp_shm_queue.c 必须排除**：monkey-mnemosyne-lib 内部有一份精简版的 `hyperamp_shm_queue.c`，已在 CMakeLists 中通过 `list(REMOVE_ITEM)` 排除，改用 front 版本的符号。**不要恢复该文件的编译**，否则会产生链接时符号冲突。

---

## 三、架构演化与系统现状

本系统从**单通道雏形**演化为如今的**三通道分离 + 跨通道桥接**架构。
**演化历程**：
- **Phase 1**：单通道原型（CH0 承载全部流量）
- **Phase 2**：CH0/CH1 双通道（解耦加解密与网络代理，消除时序冲突）
- **Phase 3（当前）**：CH0/CH1/CH2 三通道（集成 monkey-mnemosyne 远程内存访问）

### 系统组件全景图
```text
┌─────────────────────────────────────────────────────┐
│                   hvisor (EL2)                      │
│         Type-1 Hypervisor / VM 管理 / IVC           │
├───────────────────────┬─────────────────────────────┤
│     Linux Zone        │        seL4 Zone            │
│                       │                             │
│  hvisor-tool          │  hyperamp-server            │
│  (CH0 connector)      │  (统一 rootserver, C+CXX)   │
│       ↕ CH0           │    ├── channel_ch0 (加解密) │
│                       │    ├── channel_ch1 (网代)   │
│  cproxy-A (CH1)       │    ├── channel_ch2 (远存)   │
│       ↕ CH1           │    └── main.c (三通道轮询)  │
│                       │                             │
│  [未来] cproxy-B(CH2) │  monkey-mnemosyne-lib       │
│       ↕ CH2           │  (C++20 静态库, extern C)   │
├───────────────────────┴─────────────────────────────┤
│          共享内存 (0x7E000000 - 0x7E3FFFFF)         │
└─────────────────────────────────────────────────────┘
```

**组件状态**：
1. **hvisor (EL2)**：提供硬件虚拟化、物理内存映射。当前版本不依赖中断。
2. **hvisor-tool (Linux)**：CH0 的 Connector 端，负责发出图像/文本的加解密请求。
3. **hyperamp-server (seL4)**：统一的后端（C+CXX 混编项目）。合并了 `front` 源码（CH1 引擎）和 `monkey-mnemosyne-lib` 静态库（CH2 引擎），在 `main.c` 中执行三通道并发轮询。
4. **HighSpeedCProxy / cproxy-A (Linux)**：当前用于 CH1 的 Connector 端，负责将 seL4 的网络代理包发往真实网卡。
5. **monkey-mnemosyne-lib (seL4)**：C++20 编写的远程内存访问库，通过 `mnemosyne_api.h`（`extern "C"`）暴露纯 C 接口。核心组件：
   - `HyperAmpBridge`：CH2 独立的 HyperAMP queue 驱动（单例，直接操作 TX/RX 队列）
   - `Protocol2Connection`：Monkey 专用 TCP 协议栈，支持 tryAlloc / readBlock / writeBlock / refBlock / unrefBlock / plainText 等操作
   - `ADL 分配器`：C++ 内存管理层，映射到 `malloc/free`
   - **该库的 `hyperamp_shm_queue.c` 已在 CMakeLists 中排除，使用 front 版本避免符号冲突。**
6. **Monkey Mnemosyne 服务端 (Linux)**：独立的 aarch64 二进制程序，运行在 Linux 侧（可以是同一台机器或远端主机），监听 TCP 端口 10100。seL4 通过 CH2 → HyperAmpBridge → cproxy → 真实网络 → 服务端 的链路与之通信。服务端详细信息见 `monkey-mnemosyne/docs/usage.md`。
7. **CH2 的 Linux 侧 Connector (未来)**：未来计划独立运行第二个 cproxy 实例 (cproxy-B) 绑定 CH2，用于 Linux 侧的远程内存访问。届时 Linux 侧将同时运行两个 cproxy 进程：cproxy-A 绑定 CH1（网络代理），cproxy-B 绑定 CH2（远程内存）。

---

## 四、核心机制：数据流与跨通道桥接 (Cross-Channel Bridging)

### 4.1 CH0 ↔ CH1 跨通道桥接

跨通道桥接实现了安全处理（CH0）与对外网络分发（CH1）的协同工作。

**数据流模型**：
1. **独立的 CH0 请求/响应**（如普通解密/验签）：
   Linux (hvisor-tool) ─CH0→ seL4 处理完成 ─CH0→ Linux (hvisor-tool)
2. **独立的 CH1 代理流**：
   seL4 (front engine) ─CH1→ Linux (cproxy) → 真实网络
3. **CH0 → CH1 跨通道桥接 (加密/解密结果对外分发)**：
   Linux 将图像发入 CH0，seL4 完成加密或解密处理后，将结果同时通过两条路径输出：
   ```text
   Linux (hvisor-tool) ──CH0──> seL4 (加解密/验签)
                                      │ 处理完成
                                      ├── CH1 ──> Linux (cproxy) ──> 外部网络  [网络分发]
                                      └── CH0 ──> Linux (hvisor-tool)           [处理确认]
   ```

**转发白名单**（见 `channel_ch0.c` 中的 switch-case）：
- `SERVICE_ENCRYPT` / `SERVICE_VERIFY_ENCRYPT` / `SERVICE_VALIDATE_ENCRYPT`
- `SERVICE_DECRYPT` / `SERVICE_VERIFY_DECRYPT` / `SERVICE_VALIDATE_DECRYPT`

**工程实现限制**：
- `channel_ch0.c` 调用 `ch0_try_forward_bulk_to_ch1` 后，数据只放入了 CH1 内部的 `F2B (Front-to-Backend)` 队列。真实写入共享内存的操作是在下一次主循环执行 `ch1_process_message()` 中的引擎运转时发生的。
- 跨通道转发依赖 `ForwardHeader` 结构 (magic=`0x48465744` / "HFWD", 12 bytes)。第一个 chunk 携带 ForwardHeader（含 service_id、is_bulk、total_len），后续 chunk 为裸数据。Linux 侧 `receiver.py` 依据此 Header 识别新传输并创建文件。

### 4.2 CH2 远程内存访问流程

CH2 的运行模式与 CH0/CH1 不同：它不是被动等待请求，而是**主动建立 TCP Session 并持续驱动数据交换**。

**生命周期**：
```text
main.c 三通道轮询 → ch2_process_message()
                      │
                      ├── 首次调用：connect_to_monkey_mnemosyne()
                      │     └── mnemosyne_sess_new() → mnemosyne_sess_connect_by_addrstr_devid()
                      │           └── TCP 连接建立 + Protocol2 Hello/Auth + tryAlloc 内存块
                      │
                      └── 后续调用：send_plain_text()
                            └── mnemosyne_sess_send_plain_text() → Protocol2Connection::plainText()
                                  └── 服务端返回 checksum
```

**关键特性**：
- **Session 持久化**：`mnemosyne_data.session` 全局保持，首次建立后在整个生命周期复用
- **plainText 交换**：当前测试模式下，每次轮询发送一条递增字母的文本（A→Z 循环），服务端计算 ASCII checksum 并返回
- **服务端配置**：IP/端口/AuthKey 在 `channel_ch2.c` 顶部宏定义中硬编码，修改后需重新编译
- **Monkey Protocol2 协议栈**：支持 tryAlloc / readBlock / writeBlock / refBlock / unrefBlock / plainText 等操作（见 `Protocol2Connection.h`）

---

## 五、共享内存与物理约定

### 5.1 物理地址与通道容量 (千万不可更改)
| 通道 | 物理基址 | 大小 | TX Queue (seL4方向) | RX Queue (seL4方向) | Data 区域 | 槽位 (Capacity) | IPC MR | 状态 |
|------|----------|------|--------------------|--------------------|-----------|-----------------|--------|----|
| CH0 | `0x7E000000` | 2MB | `0x7E000000` | `0x7E001000` | `0x7E002000` | 256 | msg[2..4] | ✅ 运行中 |
| CH1 | `0x7E200000` | 1MB | `0x7E200000` | `0x7E201000` | `0x7E202000` | 253 | msg[5..7] | ✅ 运行中 |
| CH2 | `0x7E300000` | 1MB | `0x7E300000` | `0x7E301000` | `0x7E302000` | 253 | msg[8..10] | ✅ 运行中 |

**容量差异说明**：CH0=256 与 CH1=253 不是笔误。容量由物理内存大小决定：CH0 有 2MB 空间，数据区可容纳大量块，取 256；CH1 只有 1MB，数据区 `(1MB - 8KB) / 4KB = 254` 个块，扣除环形队列区分满/空所需的 1 个空槽后，上限为 253。**不要擅自统一这两个值。**

**Bulk 传输最大文件限制**：CH0 的 Data Region 大小约为 `2MB - 8KB ≈ 2040KB`。这是单次 Bulk 传输的文件大小上限。超过此限制需要扩大 hvisor 层面的物理内存分配。

*注意：TX/RX 的视角是以 seL4 为主的。对于 Linux 来说方向正好相反。*

地址在内核态 `boot.c` 初始化，并通过 IPC buffer 的 `msg[2..10]` 传递给 rootserver。`main.c` 在入口处**一次性读取所有 11 个 MR 值**（CH0: MR 2-4, CH1: MR 5-7, CH2: MR 8-10），然后通过 `mnemosyne_engine_init_with_vaddrs()` 将 CH2 地址传入 monkey-mnemosyne 引擎。**不要在 seL4_GetMR() 调用之前插入任何 seL4 系统调用**，否则 IPC buffer 会被覆盖。

### 5.2 Creator/Connector 身份逻辑
为避免两个 VM 启动时死锁或竞态条件，系统强制规范身份：
- **Creator (seL4 侧)**：负责 `magic` 初始化、`head/tail` 重置。
- **Connector (Linux 侧)**：在 `hyperamp_linux_init` 时第二个参数传 `0`。只负责检测 `magic` 是否就绪，绝不清理队列。

---

## 六、隐性工程知识与历史经验池 (Implicit Knowledge)

这是大量排雷过程留下的工程教训，任何修改前必须参考：

### 1. 缓存一致性迷雾 (Cache Coherency)
**现象**：偶尔发生数据包截断、脏数据、头端乱码。
**真相**：Linux 写入的数据停在 L1/L2 Cache 未刷入主存，或 seL4 直接读取了脏 Cache。
**教训**：**绝对不要**绕过 `channel.c` 中提供的 `shm_read_buffer()` 和 `shm_write_buffer()`。手动从 Data Region 读写数据前后，必须严格执行 `hyperamp_cache_invalidate` 与 `hyperamp_cache_clean`。

### 2. Bulk 内存耗尽与引擎饥饿
**历史**：过去在发送大图时，seL4 经常发生 `insufficient memory resource` 错误并导致系统超时。
**原因**：Bulk 数据被切分成几百个 4096 字节的块（Segment）并挤入 `F2B` 队列。seL4 的堆内存被瞬间抽干。
**修复约定**：在 `channel_ch0.c` 的转发循环中，现在每成功入队一次都强制执行一遍 `frontend_engine_run_hyperamp_once()`，以促使引擎立即刷出内存，保证大文件传输时堆内存始终维持低水位。**不要删除这个强制唤醒。**

### 3. 返回值 0 (`HYPERAMP_OK`) 陷阱
**历史**：`ch1_forward_bulk_raw_data` 曾返回 0 表示发送成功，导致外层 `pos += ret` 彻底陷入无限循环 (0 递增)。
**修复约定**：转发接口（非协议 Header 接口）必须透传底层成功发送的真实字节数，不要用 `HYPERAMP_OK` 替代。

### 4. 网络代理的首次超时 (First-time Session Overhead)
**现象**：`hyperamp_linux` 第一次执行会抛出 `Timeout waiting for Bulk response`，但文件依然能在 receiver.py 侧生成，第二次之后全部正常。
**原因**：首次通信需要 CH1 通过 `CREATE` 和 `RESP` 包与后端建立 Session，这个 UDP 握手的延迟导致总体时间微超出客户端内硬编码的 5 秒超时。
**教训**：这是正常的架构开销，不是 bug。

### 5. 串口 I/O 导致的性能雪崩
**历史**：开启 `DUMP_BUFFER_CONTENT` 宏后，大文件传输时向串口输出大量二进制数据，由于串口速率极低，导致发送任务被严重阻塞，引发超时。
**教训**：`frontend_api.c` 中的二进制打印宏必须保持注释状态。调试完毕后务必关闭。

### 6. monkey-mnemosyne 集成的三个关键约束
**（a）符号冲突**：monkey-mnemosyne 内部有一份精简版 `hyperamp_shm_queue.c`（删除了 `common_utils.h` 依赖和 debug 打印）。链接到 hyperamp-server 时会与 front 版本产生双重定义。当前已在 `monkey-mnemosyne/CMakeLists.txt` 中通过 `list(REMOVE_ITEM)` 排除，使用 front 版本的符号。**不要恢复该文件的编译。**

**（b）IPC MR 读取时序 (方案 A)**：原版 `mnemosyne_engine_init()` 内部自行调用 `seL4_GetMR(8/9/10)`，但在三通道模式中 `main.c` 会在初始化 CH0/CH1 的过程中触发 syscall 导致 IPC buffer 被覆盖。因此新增了 `mnemosyne_engine_init_with_vaddrs()` 接口，由 `main.c` 统一在最开头读取所有 MR 值后传入。**不要改回使用原版 `mnemosyne_engine_init()`，除非能保证在调用前没有任何 syscall。**

**（c）C/C++ 混编**：hyperamp-server 的 CMakeLists 已从 `project(... C)` 升级为 `project(... C CXX)`，链接 `monkey-mnemosyne-lib` C++ 静态库。monkey-mnemosyne 的子目录通过 `if(NOT TARGET sel4)` 跳过重复的 `find_package`，避免 seL4 库目标冲突。

---

## 七、构建指南与代码导航 (Agent Quickstart)

### 编译流水线
```bash
cd sel4test
rm -rf cbuild && mkdir cbuild && cd cbuild
../init-build.sh -DPLATFORM=imx8mp-evk -DAARCH64=1 -DSel4testApp=hyperamp-server
ninja
```

### 代码阅读链路 (推荐顺序)
1. **基石层**：`front/include/hyperamp_shm_queue.h` 和 `front/src/hyperamp_shm_queue.c` (队列核心)
2. **入口层**：`hyperamp-server/src/main.c` (IPC 读取，三通道初始化，轮询主循环)
3. **协议层**：`hyperamp-server/src/channel.c` (含安全的 Cache 维护封装)
4. **业务层 CH0**：`hyperamp-server/src/channel_ch0.c` (加解密服务 + `ch0_try_forward_bulk_to_ch1` 桥接点)
5. **业务层 CH1**：`hyperamp-server/src/channel_ch1.c` (向 cproxy 对接的网络前置栈 + `ch1_forward_bulk_raw_data`)
6. **业务层 CH2**：`hyperamp-server/src/channel_ch2.c` (Session 建立 + plainText 发送循环 + mnemosyne API 封装)
7. **CH2 C 接口**：`monkey-mnemosyne/include/mnemosyne_api.h` → `monkey-mnemosyne/src/mnemosyne_api.cc` (纯 C facade → C++ 实现)
8. **CH2 协议栈**：`monkey-mnemosyne/src/monkey/net/protocol/Protocol2Connection.cc` (Monkey Protocol2：tryAlloc / readBlock / writeBlock / plainText 等)
9. **CH2 网络栈**：`monkey-mnemosyne/src/monkey/net/HyperAmpBridge.cc` (CH2 独立的 HyperAMP queue 驱动、Session 创建和数据收发)
10. **CH2 文档**：`monkey-mnemosyne/docs/usage.md` (服务端部署、客户端配置、测试方案)

### 验证工具链
- **receiver.py** (`front/tools/receiver.py`)：监听 UDP 端口接收 cproxy 转发的数据，解析 `ForwardHeader` 后根据8888和8889落盘为 `.txt/.png` 文件。用于端到端验证加解密结果的正确性。
  ```bash
  # 在 Linux 侧运行（监听 Bulk 端口 8889）
ip netns exec ns1 python3 receiver.py --port 8888 --dir ./fwd_text &
ip netns exec ns1 python3 receiver.py --port 8889 --dir ./fwd_binary &
ip netns exec ns1 ./cproxy > cproxy.log 2>&1 &

  ```
- **Monkey Mnemosyne 服务端**：需要在可达的 Linux 主机上运行（详见 `monkey-mnemosyne/docs/usage.md`）。seL4 启动后 CH2 会自动连接。
  ```bash
  # 在 Linux 侧运行服务端（默认端口 10100）
  #cproxy-ch2和cproxy本质都是HighSpeedCProxy编译出来的，只是用到通道地址不同，cproxy-ch2用的通道地址为SHM_CH1_PADDR，cproxy用的通道地址为SHM_CH0_PADDR
  ip netns exec ns1 ./cproxy-ch2 > cproxy-ch2.log 2>&1 &
  ./monkey-mnemosyne &
  ```

---

## 八、Agent 自检清单

**在修改 `channel_ch*.c`、共享内存、queue、cache 相关代码前，你必须先确认自己能回答以下问题。如果无法回答，请先回读本文档对应章节，不要直接动手改代码。**

1. **Creator/Connector**：当前系统中谁是 Creator、谁是 Connector？为什么不能反过来？
2. **通道分工**：CH0 处理什么？CH1 处理什么？CH2 处理什么？跨通道数据是怎么流转的？
3. **Cache 封装**：读写共享内存时必须经过哪两个函数？如果绕过会发生什么？
4. **Queue 所有权**：`head` 由谁推进？`tail` 由谁推进？`head == tail` 意味着什么？
5. **Bulk 分块生命周期**：为什么转发循环中每次入队都要强制执行 `frontend_engine_run_hyperamp_once()`？删掉会怎样？
6. **CH2 集成约束**：`mnemosyne_engine_init_with_vaddrs()` 与 `mnemosyne_engine_init()` 的区别是什么？为什么三通道模式下必须使用前者？
7. **符号冲突**：monkey-mnemosyne 内部的 `hyperamp_shm_queue.c` 为什么被排除？如果恢复编译会发生什么？
