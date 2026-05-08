# HyperAMP 多通道跨虚拟机通信系统 — 工程上下文

## 角色定义

你正在参与一个基于 hvisor（Type-1 Hypervisor）+ seL4 + Linux + HyperAMP 共享内存通信构建的跨虚拟机高性能通信系统。

你的职责不是重新设计架构，而是在理解现有系统的基础上继续实现与扩展。

**你必须严格保持：**
- 现有共享内存布局和物理地址分配
- HyperampShmQueue 数据结构和 spinlock 语义
- creator / connector 初始化角色约定
- polling 通信模式（不使用中断）
- cache invalidate / clean 一致性逻辑
- 多通道协议隔离（CH0 和 CH1 消息域不混用）

**你不要随意：**
- 重构通信协议或消息格式
- 修改共享内存物理地址布局
- 引入新的 IPC 模型（信号量、事件通知等）
- 将 polling 改为 interrupt
- 删除任何 cache maintenance 代码

---

## 1. 系统整体架构

系统由四个核心项目组成，协同实现跨虚拟机安全通信：

```
┌─────────────────────────────────────────────────────┐
│                   hvisor (EL2)                      │
│         Type-1 Hypervisor / VM 管理 / IVC           │
├───────────────────────┬─────────────────────────────┤
│     Linux Zone        │        seL4 Zone            │
│                       │                             │
│  hvisor-tool          │  hyperamp-server            │
│  (CH0 connector)      │  (统一 rootserver)          │
│       ↕ CH0           │    ├── channel_ch0 (加解密) │
│                       │    ├── channel_ch1 (网络代理)│
│  HighSpeedCProxy      │    └── main.c (双通道轮询)  │
│  (CH1 connector)      │                             │
│       ↕ CH1           │                             │
├───────────────────────┴─────────────────────────────┤
│          共享内存 (0x7E000000 - 0x7E3FFFFF)         │
└─────────────────────────────────────────────────────┘
```

### 1.1 hvisor

Type-1 Hypervisor，运行在 EL2。

职责：
- VM 生命周期管理（启动 seL4 Zone 和 Linux Zone）
- 共享内存物理页映射到各 Zone
- HyperAMP 通信底层支撑
- IVC（Inter-VM Communication）机制

当前通信机制：基于共享内存 + polling，**不使用中断**。

### 1.2 hvisor-tool

Linux 侧的 CH0 通信工具。

职责：
- 通过 `/dev/hvisor` 映射共享内存物理页
- 作为 **connector** 连接 seL4 已创建的 CH0 队列
- 发送图片/文本加解密请求、Bulk 数据传输

关键文件：
- `hvisor-tool/tools/shm/hyperamp_linux_shm.c` — 队列初始化和收发逻辑
- `hvisor-tool/tools/shm/hyperamp_linux_shm.h` — 队列结构定义

**注意**：该工具调用 `hyperamp_linux_init(channel, is_creator)`，CH0 场景下 `is_creator=0`。

### 1.3 sel4test（seL4 侧应用）

seL4 侧只有一个统一的 rootserver 应用 `hyperamp-server`，它内部同时处理 CH0（加解密/验证）和 CH1（网络代理）两条通道。

**重要：`front` 项目的代码已经被合并到 `hyperamp-server` 的构建系统中，不再作为独立应用运行。**

#### hyperamp-server（统一 rootserver）

构建产物：`hyperamp-server-image-arm-imx8mp-evk`

内部模块划分：

| 模块 | 文件 | 职责 |
|------|------|------|
| 主循环 | `src/main.c` | IPC 地址读取、双通道初始化、并发轮询 |
| 通道抽象 | `src/channel.c`, `include/channel.h` | `ChannelContext` 结构体、cache 安全读写 |
| CH0 处理 | `src/channel_ch0.c`, `include/channel_ch0.h` | Bulk/加密/解密/签名验证/字段校验 |
| CH1 处理 | `src/channel_ch1.c`, `include/channel_ch1.h` | 封装 front 引擎、Session 管理、跨通道转发接口 |
| CH0 辅助 | `include/ch0_utils.h` | 安全内存操作宏（从旧版 hyperamp-server 提取） |

构建系统关键规则（`CMakeLists.txt`）：
- `front/include` 在 include 路径中**优先于** `hyperamp-server/include`，确保使用 front 版本的 `hyperamp_shm_queue.h`
- `front/src/*.c` 通过 `${FRONT_DIR}/src/` 直接引用，不使用 symlink
- front 源文件和 `channel_ch1.c` 单独设置 `-Wno-error`，其余保持 `-Werror`

#### front（网络代理前端源码库）

**front 不再独立构建和运行**，它的源文件被 hyperamp-server 的 CMakeLists.txt 直接引用编译。

front 提供的核心能力：
- `frontend_sess_new()` / `frontend_sess_send()` / `frontend_sess_recv()` — Session API
- `frontend_engine_run_hyperamp_once()` — 单轮消息处理（出队→协议解析→会话回调→F2B 发送）
- `FrontendEngine` / `FrontendSession` / `FrontendSessionPool` — 会话管理框架
- `hyperamp_shm_queue.c/h` — 统一的队列实现（front 版本为准）

关键头文件：
- `front/include/engine.h` — FrontendEngine 定义
- `front/include/frontend_api.h` — 对外 API 声明
- `front/include/session.h` — Session 和 IoT 会话定义
- `front/include/hyperamp_shm_queue.h` — 队列结构（统一版本）
- `front/include/message.h` — 代理协议消息定义

### 1.4 HighSpeedCProxy

Linux 侧网络代理后端。

职责：
- 作为 **connector** 连接 seL4 已创建的 CH1 队列
- 接收 seL4 前端发来的 Session 管理和 DATA 消息
- 通过真实 Linux 网络 socket 对外转发数据
- 管理 epoll 事件循环和网络 I/O

关键文件：
- `HighSpeedCProxy/src/engine.c` — 后端引擎主循环
- `HighSpeedCProxy/src/backend_proto.c` — 后端协议解析
- `HighSpeedCProxy/src/shared_mem_io.c` — 共享内存 I/O
- `HighSpeedCProxy/include/hyperamp_shm_queue.h` — 队列结构（与 front 版本一致）

---

## 2. 共享内存布局

### 2.1 每个通道的内部结构

```
偏移 0x0000: RX Queue 控制块 (4KB) — Linux→seL4 方向
偏移 0x1000: TX Queue 控制块 (4KB) — seL4→Linux 方向
偏移 0x2000: Data Region (剩余空间) — Bulk 数据零拷贝区
```

**注意 TX/RX 方向是相对于 seL4 的**。Linux 侧看到的方向相反：
- seL4 的 TX Queue = Linux 的 RX Queue
- seL4 的 RX Queue = Linux 的 TX Queue

### 2.2 i.MX8MP 平台物理地址分配

| 通道 | 物理基址 | 大小 | TX Queue PA | RX Queue PA | Data PA |
|------|----------|------|-------------|-------------|---------|
| CH0 | `0x7E000000` | 2MB | `0x7E000000` | `0x7E001000` | `0x7E002000` |
| CH1 | `0x7E200000` | 1MB | `0x7E200000` | `0x7E201000` | `0x7E202000` |
| CH2 | `0x7E300000` | 1MB | `0x7E300000` | `0x7E301000` | `0x7E302000` |

这些地址由 hvisor 在启动时映射，由 seL4 内核 `boot.c` 转换为虚拟地址并通过 IPC buffer `msg[2..10]` 传递给 rootserver。

**绝对不要修改这些物理地址。**

### 2.3 IPC Buffer 地址传递约定

seL4 内核 `boot.c` 将虚拟地址写入 IPC buffer：

```
msg[2] = CH0 TX vaddr    msg[3] = CH0 RX vaddr    msg[4] = CH0 Data vaddr
msg[5] = CH1 TX vaddr    msg[6] = CH1 RX vaddr    msg[7] = CH1 Data vaddr
msg[8] = CH2 TX vaddr    msg[9] = CH2 RX vaddr    msg[10] = CH2 Data vaddr
```

**关键约束**：`seL4_GetMR()` 必须在任何 seL4 系统调用之前调用，否则 IPC buffer 内容会被覆盖。

---

## 3. 通道设计（已确定，不要修改）

### CH0 — 安全数据处理通道

| 属性 | 值 |
|------|-----|
| 用途 | 图片/文本加解密、签名验证、字段校验、Bulk 数据处理 |
| seL4 角色 | **creator**（初始化队列） |
| Linux 角色 | **connector**（连接已有队列） |
| seL4 应用 | hyperamp-server / channel_ch0 |
| Linux 应用 | hvisor-tool (hyperamp_linux_shm) |
| 队列容量 | 256 |
| 允许消息类型 | `HYPERAMP_MSG_TYPE_BULK`, `HYPERAMP_MSG_TYPE_SERVICE`, `SERVICE_*` |

### CH1 — 网络代理通道

| 属性 | 值 |
|------|-----|
| 用途 | 网络代理前后端通信、跨通道转发的数据出口 |
| seL4 角色 | **creator**（初始化队列） |
| Linux 角色 | **connector**（连接已有队列） |
| seL4 应用 | hyperamp-server / channel_ch1（封装 front 引擎） |
| Linux 应用 | HighSpeedCProxy |
| 队列容量 | 253 |
| 允许消息类型 | `PROXY_MSG_TYPE_*`（Session/Data/Device/Strategy） |

### CH2 — 预留

暂不实现，地址已分配，通道定义已保留。

---

## 4. Creator / Connector 规则（极其重要）

每个物理通道只能有**一个 creator**。不同通道之间互不影响。

| 行为 | Creator | Connector |
|------|---------|-----------|
| 初始化 queue 控制块 | ✅ 写入 magic、capacity、block_size | ❌ 不写入 |
| 重置 head/tail 指针 | ✅ 清零 | ❌ 不碰 |
| 清空 data region | ✅ 可选 | ❌ 不碰 |
| 使用时机 | 先启动的一方 | 后连接的一方 |

**当前约定**：
- seL4 = creator（两个通道都是 seL4 先初始化）
- Linux = connector

**典型错误**：Linux 侧调用 `hyperamp_linux_init(channel, 1)` 意外走了 creator 分支，覆盖了 seL4 已建立的队列状态，导致双方状态机错乱、消息丢失。

正确用法：
```c
// Linux CH0: connector
hyperamp_linux_init(0, 0);

// Linux CH1: connector
hyperamp_linux_init(1, 0);
```

---

## 5. 数据流与通道桥接

### 5.1 CH0 独立数据流（加解密请求-响应）

```
Linux (hvisor-tool) ──CH0 RX──> seL4 (channel_ch0)
                                      │ 处理（加密/解密/验签）
Linux (hvisor-tool) <──CH0 TX── seL4 (ch0_send_reply / ch0_send_bulk_reply)
```

### 5.2 CH1 独立数据流（网络代理）

```
seL4 (channel_ch1) ──CH1 TX──> Linux (HighSpeedCProxy) ──> 外部网络
seL4 (channel_ch1) <──CH1 RX── Linux (HighSpeedCProxy)
```

### 5.3 CH0 → CH1 跨通道桥接（安全处理-可信分发）

这是系统的核心架构亮点。CH0 处理完加密类请求后，结果同时通过两条路径输出：

```
Linux (hvisor-tool) ──CH0──> seL4 (加解密/验签)
                                   │ 处理完成
                                   ├── CH1 ──> Linux (cproxy) ──> 外部网络   [网络分发]
                                   └── CH0 ──> Linux (hvisor-tool)            [处理确认]
```

实现机制：
- `channel_ch0.c` 在加密完成后调用 `ch0_try_forward_to_ch1()` 或 `ch0_try_forward_bulk_to_ch1()`
- 这些函数内部调用 `ch1_forward_data()`（定义在 `channel_ch1.c`）
- `ch1_forward_data()` 通过已建立的 `FrontendSession` 调用 `frontend_sess_send()`
- 由于 CH0 和 CH1 在同一进程中，跨通道调用仅是函数调用，无需 IPC

**选择性转发策略**：
- 加密类服务（`SERVICE_ENCRYPT`、`SERVICE_VERIFY_ENCRYPT`、`SERVICE_VALIDATE_ENCRYPT`）→ 结果通过 CH1 分发
- 解密类/验签类 → 仅通过 CH0 返回给请求方
- CH1 未就绪时 → 静默跳过，不影响 CH0 正常应答（优雅降级）

---

## 6. Polling 通信模型

当前系统完全基于 polling，不使用中断。

seL4 侧主循环（`main.c`）：
```c
while (1) {
    // 轮询 CH0：处理加解密/验证请求
    ch0_process_message(&g_ch0);

    // 轮询 CH1：处理网络代理请求（含跨通道转发的实际发送）
    ch1_process_message(&g_ch1);

    // 空闲时轻量延迟
}
```

`ch1_process_message()` 内部调用 `frontend_engine_run_hyperamp_once()`，它负责：
1. 从 CH1 RX Queue 出队消息（Linux→seL4 方向）
2. 协议解析（`frontend_proxy_msg_process`）
3. 处理 B2F 活跃队列回调
4. **将 F2B 队列中的待发数据写入 CH1 TX Queue**（包括跨通道桥接的数据）

第 4 步至关重要：`ch0_try_forward_to_ch1()` 只是将数据放入 Session 的 F2B 消息队列，真正写入共享内存是在下一次 `ch1_process_message()` 时完成的。

---

## 7. Cache 一致性规则（非常重要）

所有通道操作必须遵守：

| 操作 | Cache 动作 | 原因 |
|------|-----------|------|
| 从共享内存**读取**数据前 | `hyperamp_cache_invalidate()` | Linux 写入的数据可能只在 RAM 中，seL4 本地缓存是脏的 |
| 向共享内存**写入**数据后 | `hyperamp_cache_clean()` | 确保 seL4 写入的数据刷回 RAM，Linux 能读到 |

尤其需要注意的场景：
- Bulk 数据传输（图片可能几百 KB 到几 MB）
- Data Region 的直接读写
- Queue 控制块的 head/tail 更新

**绝大多数"偶现数据错误"都与 cache 有关。不要删除任何 cache maintenance 代码。**

`channel.c` 中的 `shm_read_buffer()` 和 `shm_write_buffer()` 已经封装了 cache 操作。直接使用这些函数可以避免遗漏。

---

## 8. Queue 数据结构要点

`HyperampShmQueue` 是环形队列，关键字段：

```c
typedef struct {
    uint32_t head;        // 写入位置（producer 推进）
    uint32_t tail;        // 读取位置（consumer 推进）
    uint16_t capacity;    // 最大槽位数
    uint16_t block_size;  // 每个槽位大小（固定 4096）
    uint32_t magic;       // 初始化标记 0x48415150 ("HAQP")
    HyperampSpinLock lock; // 跨 VM 自旋锁
    // ... 统计计数器
} HyperampShmQueue;
```

**关键约束**：
- `block_size` 固定 4096 字节，消息（含 header）不能超过此大小
- `capacity` 由通道内存大小决定（CH0=256, CH1=253）
- `magic` 用于 connector 判断 creator 是否已初始化
- `head == tail` 表示队列为空

---

## 9. 构建与部署

### 编译

```bash
cd sel4test
rm -rf cbuild && mkdir cbuild && cd cbuild
../init-build.sh -DPLATFORM=imx8mp-evk -DAARCH64=1 -DSel4testApp=hyperamp-server
ninja
```

产物：`cbuild/images/hyperamp-server-image-arm-imx8mp-evk`

### 运行测试步骤

1. 启动 Linux Zone
2. 运行网络代理后端：`./cproxy > cproxy.log 2>&1 &`
3. 启动 seL4 Zone（加载 hyperamp-server 镜像）
4. 等待 cproxy.log 中出现 `high_speed_create_sess_active returns successfully!`（CH1 Session 建立）
5. 发送加密请求：`./hyperamp_linux -B -e @received.png -o encrypted.bin`
6. 验证：
   - seL4 串口出现 `[CH0→CH1] ✓ Bulk 数据网络分发完成` → 跨通道桥接成功
   - cproxy.log 出现 DATA 消息 → Linux 后端收到加密数据
   - `encrypted.bin` 正常生成 → CH0 应答路径正常

---

## 10. 类型安全约定

- seL4 用户态代码**必须使用 `seL4_Word`**，不要使用内核态的 `word_t`
- IPC buffer 读取使用 `seL4_GetMR()`，返回类型为 `seL4_Word`
- 共享内存指针必须标记 `volatile`
- `HYPERAMP_AGAIN` 返回值统一为 `1`（front 版本），不要使用旧版的 `-2`

---

## 11. 代码阅读顺序

进入项目后按以下顺序建立理解：

**第一层：理解队列和共享内存**
1. `front/include/hyperamp_shm_queue.h` — 队列结构定义
2. `front/src/hyperamp_shm_queue.c` — 队列 enqueue/dequeue 实现
3. `hyperamp-server/src/channel.c` — cache 安全的读写封装

**第二层：理解通道抽象和主循环**
4. `hyperamp-server/include/channel.h` — ChannelContext 结构
5. `hyperamp-server/src/main.c` — IPC 地址读取、双通道初始化、轮询循环

**第三层：理解业务处理**
6. `hyperamp-server/src/channel_ch0.c` — CH0 加解密/验签/Bulk 处理 + 跨通道转发
7. `hyperamp-server/src/channel_ch1.c` — CH1 前端引擎封装 + Session 管理 + `ch1_forward_data`

**第四层：理解网络代理协议栈（按需）**
8. `front/src/engine.c` — FrontendEngine 主循环
9. `front/include/frontend_api.h` — Session API 声明
10. `HighSpeedCProxy/src/engine.c` — 后端引擎（Linux 侧）

---

## 12. 工程思维方式

这个项目不是 socket 通信、RPC 框架或普通 IPC。它是：

- **低层共享内存通信** — 直接操作物理内存映射
- **Cache 敏感** — 每次读写都必须考虑缓存一致性
- **物理地址绑定** — 地址由 hypervisor 在启动时固定
- **Queue 驱动** — 所有通信通过 SPSC 环形队列
- **Polling 驱动** — 无中断，主循环持续轮询

你必须始终关注：
- 物理内存所有权（谁写了这块内存？cache 是否同步？）
- Queue 的 producer/consumer 正确性（head/tail 谁推进？）
- 跨 VM 并发安全（spinlock 的 acquire/release 语义）
- 通道隔离（CH0 消息不要混入 CH1 处理流程）

而不是高层网络抽象。
