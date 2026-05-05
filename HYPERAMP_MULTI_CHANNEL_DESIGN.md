# HyperAMP 多通道合并设计方案

## 1. 背景

当前仓库里已经存在三套相关能力：

- `hvisor`：负责启动 seL4 和 Linux Zone，提供 IVC / HyperAMP 的底层支撑。
- `hvisor-tool`：Linux 侧控制与共享内存通信工具，包含 HyperAMP Linux 端队列实现。
- `sel4test/projects/sel4test/apps/front`：seL4 侧网络代理前端。
- `sel4test/projects/sel4test/apps/hyperamp-server`：seL4 侧图片/文本加解密与验证服务。
- `HighSpeedCProxy`：Linux 侧网络代理后端。

目前的目标不是把所有功能混在一条通道里，而是把 HyperAMP 拆成明确的多通道结构，让不同业务隔离、互不干扰。

## 2. 最终目标

### 2.1 通道规划

- CH0：给 `hyperamp-server` 使用，用于图片、文本、加解密及验证。
- CH1：给 `front` + `HighSpeedCProxy` 使用，用于网络代理前后端通信。
- CH2：预留，后续再给新的 Linux 或 seL4 应用使用。

### 2.2 业务划分

- Linux 端应用 1：`hvisor-tool/tools/shm/hyperamp_linux_shm.c` 相关程序，向 seL4 发送待处理的图片或文本请求，使用 CH0。
- Linux 端应用 2：`HighSpeedCProxy`，负责实际处理 seL4 端网络请求，使用 CH1。
- seL4 端应用：一个进程内同时轮询 CH0 和 CH1。
  - CH0 收到图片/字符数据后，执行处理、加解密、验证。
  - CH1 收到网络代理消息后，走 front 的网络代理协议。
  - 处理完成后，通过 CH1 将结果发送回 Linux。

## 3. 现状分析

### 3.1 当前代码现状

#### hyperamp-server

`sel4test/projects/sel4test/apps/hyperamp-server/src/main.c` 当前按 CH0 初始化，物理地址为：

- TX Queue: `0x7E000000`
- RX Queue: `0x7E001000`
- Data Region: `0x7E002000`

它已经包含以下处理逻辑：

- `process_bulk_message()`
- `verify_signed_data()`
- `validate_mission_data()`
- `send_bulk_reply()`
- `send_reply_to_linux()`

并且在处理大块数据时已经正确使用了：

- `hyperamp_cache_invalidate()`
- `hyperamp_cache_clean()`

#### front

`sel4test/projects/sel4test/apps/front/src/main.c` 当前也在使用 CH0 的队列配置，当前 HyperAMP 队列配置来自 `hyperamp_shm_queue.h` 中的 CH0 地址分支。

front 的核心链路已经具备：

- `frontend_sess_new()`
- `frontend_sess_connect_by_addrstr()`
- `frontend_sess_send()`
- `frontend_engine_run_hyperamp_once()`

#### HighSpeedCProxy

`HighSpeedCProxy` 目前通过 `engine_init_hyperamp_queue()` 调用 `hyperamp_linux_init(0, 0)`，默认映射也是 CH0 方向的地址布局。它的 HyperAMP 处理链路已经是轮询模式：

- `engine_init()`
- `engine_init_hyperamp_queue()`
- `backend_engine_hyperamp_rx_queue_get()`
- `backend_proxy_msg_process()`

### 3.2 当前问题

当前问题不是“能不能通信”，而是“多个业务如何共享不同通道而不串线”。

如果不做通道隔离，会出现：

- 图片加解密请求和网络代理请求混入同一通道。
- 不同消息头、不同服务 ID、不同 payload 格式互相冲突。
- creator / connector 角色混乱，导致初始化时覆盖掉对端状态。
- cache 维护和数据区访问规则难以统一。

## 4. 通道与角色分配

### 4.1 通道矩阵

| 通道 | seL4 侧角色 | Linux 侧角色 | 业务 |
|---|---|---|---|
| CH0 | creator | connector | 图片、文本、加解密、验证 |
| CH1 | creator | connector | 网络代理前后端 |
| CH2 | 预留 | 预留 | 后续新增应用 |

### 4.2 为什么 creator 不需要全局统一

creator 不是全局属性，而是“每个物理通道的初始化责任归属”。

也就是说：

- CH0 上只能有一个 creator。
- CH1 上只能有一个 creator。
- CH2 上只能有一个 creator。

不同通道之间互不影响，不需要全局统一 creator 角色。

### 4.3 为什么 `hyperamp_linux_init(0,1)` 会影响收消息

`hyperamp_linux_init()` 的 creator 分支会执行：

- TX queue 初始化
- RX queue 初始化
- data region 清空

这意味着如果 Linux 侧本来只是想“接入已有通道”，却错误地走了 creator 分支，就有可能覆盖掉已经建立好的队列状态，导致收不到消息。

因此：

- 如果 Linux 侧是接入 seL4 已经创建好的通道，应使用 `hyperamp_linux_init(..., 0)`。
- 如果 Linux 侧是该通道的创建者，才使用 `hyperamp_linux_init(..., 1)`。

## 5. 地址规划

### 5.1 i.MX8MP 平台物理地址

当前已经确认 i.MX8MP 平台使用以下共享内存起始地址：

- CH0: `0x7E000000`
- CH1: `0x7E200000`
- CH2: `0x7E300000`

每个通道内部布局为：

- RX Queue: 4KB
- TX Queue: 4KB
- Data Region: 剩余空间

### 5.2 建议保持的固定约定

建议后续代码中明确写成：

```c
// CH0
SHM0_TX_QUEUE_PADDR = 0x7E000000
SHM0_RX_QUEUE_PADDR = 0x7E001000
SHM0_DATA_PADDR     = 0x7E002000

// CH1
SHM1_TX_QUEUE_PADDR = 0x7E200000
SHM1_RX_QUEUE_PADDR = 0x7E201000
SHM1_DATA_PADDR     = 0x7E202000

// CH2
SHM2_TX_QUEUE_PADDR = 0x7E300000
SHM2_RX_QUEUE_PADDR = 0x7E301000
SHM2_DATA_PADDR     = 0x7E302000
```

具体是否保留 1MB 或 2MB 的映射大小，要看后续通道是否需要大数据区域。

## 6. 消息域隔离

### 6.1 CH0 消息域

CH0 只承载图片/文本处理消息，建议只使用以下语义：

- `HYPERAMP_MSG_TYPE_BULK`
- `SERVICE_ENCRYPT`
- `SERVICE_DECRYPT`
- `SERVICE_VERIFY_ONLY`
- `SERVICE_VERIFY_ENCRYPT`
- `SERVICE_VERIFY_DECRYPT`
- `SERVICE_VALIDATE_ENCRYPT`
- `SERVICE_VALIDATE_DECRYPT`

### 6.2 CH1 消息域

CH1 只承载网络代理消息，建议只使用：

- `HYPERAMP_MSG_TYPE_DEV`
- `HYPERAMP_MSG_TYPE_STRGY`
- `HYPERAMP_MSG_TYPE_SESS`
- `HYPERAMP_MSG_TYPE_DATA`

以及 front / HighSpeedCProxy 当前已有的 `ProxyMsgHeader` 语义。

### 6.3 CH2 消息域

CH2 预留，不做实现，但需要保留通道定义和地址规划，确保后续扩展时不再改动前面两个通道的约定。

## 7. Cache 与数据区规则

### 7.1 CH0 / CH1 / CH2 都要遵守的规则

- 队列控制区要保证一致性。
- 数据区如果是大块数据，读前必须 invalidate。
- 处理后如果会被对端读取，写回前必须 clean。
- 每个通道单独维护自己的 cache 处理，不共享 cache 逻辑。

### 7.2 适用范围

- CH0 的图片处理：必须保留 `invalidate + clean`。
- CH1 的网络代理：如果 payload 很小，可以主要依赖队列结构本身；如果未来 CH1 也承载大块数据，则同样需要 `invalidate + clean`。
- CH2：预留，不实现，但规则保持一致。

## 8. 单进程轮询调度方案

当前优先选择轮询是可行的，建议在 seL4 侧做成一个统一主循环：

1. 轮询 CH0。
2. 轮询 CH1。
3. 如果 CH2 将来启用，再加入 CH2。
4. 每次循环可设置每通道最大处理条数，避免某个通道占满 CPU。

### 8.1 推荐策略

如果你当前只想先稳定跑通，建议：

- 先采用简单轮询。
- 优先 CH0 或按固定顺序轮询都可以。
- 不做复杂调度。

后续如果发现 CH0 图片处理量大、CH1 网络消息也频繁，再改成配额式轮询。

## 9. 建议的代码改造方向

### 9.1 seL4 侧

#### hyperamp-server

- 保持 CH0。
- 保留 Bulk / 验证 / 加解密逻辑。
- 抽象成 `ch0` 通道模块。

#### front

- 切换到 CH1。
- 保留 session / data / network proxy 逻辑。
- 抽象成 `ch1` 通道模块。

#### 合并后的 seL4 应用

建议最终合并成一个应用，内部拆为：

- `channel_ch0.c/h`
- `channel_ch1.c/h`
- `main.c`

其中：

- `main.c` 负责初始化两个通道并轮询。
- `channel_ch0` 负责图片/文本处理。
- `channel_ch1` 负责网络代理。

### 9.2 Linux 侧

#### hvisor-tool

- `hyperamp_linux_shm.c` 保持 CH0 逻辑，用于图片/文本处理。
- 后续如果要支持 CH1/CH2，可再扩展为多实例或新封装。

#### HighSpeedCProxy

- 保持 CH1 逻辑。
- 初始化时明确使用 CH1 的起始地址，不再默认指向 CH0。

## 10. 推荐的实施顺序

### 第 1 步

先把通道地址和角色固定下来：

- CH0 = hyperamp-server
- CH1 = front / HighSpeedCProxy
- CH2 = 预留

### 第 2 步

把 seL4 侧两个应用合并为一个进程中的两个模块：

- CH0 处理模块
- CH1 处理模块

### 第 3 步

Linux 侧分别校准初始化参数：

- CH0 使用 `hyperamp_linux_init(..., 0)`。
- CH1 也使用 `hyperamp_linux_init(..., 0)`，如果是接入 seL4 侧已创建的通道。

### 第 4 步

确认轮询主循环的调度顺序：

- 简单轮询即可。
- 先跑通功能，再优化公平性。

## 11. 风险点

### 11.1 creator / connector 配错

这是最容易导致“收不到消息”的问题。必须保证：

- 一个通道只能有一个 creator。
- 另一个端只能 connector。

### 11.2 通道地址不一致

如果 seL4 和 Linux 的地址定义不一致，就会出现映射成功但两边看到的不是同一块内存。

### 11.3 消息类型混用

如果 CH0 和 CH1 共用同一套消息类型判断，后续一定会出现协议串线。

### 11.4 cache 处理遗漏

大块图片数据、签名数据、验证数据都可能因为 cache 没处理好而出现“看起来像随机错误”的问题。

## 12. 结论

这个多通道方案是明确且可行的。

你现在真正要落定的是：

- CH0 负责图片/文本加解密与验证。
- CH1 负责网络代理。
- CH2 预留。
- 每个通道内部自己有唯一 creator。
- seL4 侧最终合并成一个应用，Linux 侧保持两个应用。
- 主循环先用轮询。

如果你确认这份方案，我下一步就可以直接开始实现，优先做：

1. seL4 侧合并入口与通道抽象。
2. Linux 侧通道参数和初始化分离。
3. 统一地址宏和消息域隔离。

## 13. 实施成果与技术沉淀

上述方案已于最新重构中全部落地，成功将 `hyperamp-server` (CH0) 与 `front` (CH1) 合并为单一的 seL4 Rootserver。在实际合并过程中，我们总结并解决了以下关键技术挑战：

### 13.1 代码复用与构建隔离 (Symlink 与 CMake 策略)

为了避免复制粘贴庞大的 `front` 网络代理协议栈代码，我们在 `hyperamp-server` 的构建系统 (`CMakeLists.txt`) 中引用的源文件列表里，直接包含了 `front/src` 下的源文件路径。
同时，由于 `front` 源文件存在若干早期的格式化警告（在 `-Werror` 下会编译失败），我们通过 CMake 的 `set_source_files_properties` 单独为这些引入的源文件和涉及空声明的模块禁用了 `-Werror`，保证了核心进程的严格编译要求与复用代码的兼容性。

### 13.2 头文件冲突与模块解耦

合并时遇到了 `hyperamp_shm_queue.h` 双版本冲突（宏定义、`HYPERAMP_AGAIN` 返回值等不一致）。
**解决方案**：
- 统一以 `front` 版本的头文件和实现为准（通过 CMake include 优先级保证）。
- 原 `hyperamp-server` (CH0) 强依赖的旧版安全内存操作宏（如 `hyperamp_safe_memcpy`）被提取到了独立的 `ch0_utils.h` 中，避免污染统一的底层库。
- 建立 `channel.h` 抽象层，采用 `channel_ch0.c` 和 `channel_ch1.c` 作为独立的业务模块入口，在统一的 `main.c` 轮询循环中调用，实现业务逻辑层面的完全解耦。

### 13.3 类型安全演进

seL4 用户态中部分旧类型（如内核态遗留的 `word_t`）已不可用。我们在重构中完成了从 `word_t` 到标准用户态类型 `seL4_Word` 的全面替换（包括 `channel.h`、IPC 缓冲读取逻辑及 `front/engine.c`）。后续扩展必须严格使用 `seL4_Word` 确保移植性。

### 13.4 网络代理状态机与初始化机制

在完全隔离的通道架构中，网络代理通信（CH1）必须依赖主动事件触发：
- **框架剥离**：移除了原 `front/src/main.c` 中硬编码的测试会话创建代码后，CH1 通道在初始化完成后会处于纯净的静默监听状态。
- **正常现象**：此时 Linux 侧 `cproxy` 不停打印 `HyperAMP RX queue is empty!` 是正常的无阻塞轮询表现。
- **如何发包**：必须由 seL4 端业务层（或网络层协议栈回调）主动发起 Session 建连，或接入真实虚拟网卡收发流程，CH1 才会有数据流通。我们在验证阶段通过在 `ch1_init` 补充测试 `frontend_sess_new` 代码，成功验证了前/后端 UDP 会话建立和数据转发的全链路。
