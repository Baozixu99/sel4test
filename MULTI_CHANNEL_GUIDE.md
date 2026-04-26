# HyperAMP 多通道机制与扩展指南

本文档说明了 `hyperamp-server` 中多通道通信的实现原理，并提供了添加新通道的操作指南。

## 1. 当前改动说明

在 `projects/sel4test/apps/hyperamp-server/src/main.c` 中：
- **修复了函数缺失问题**：实现了 `send_reply_to_linux` 包装函数。
- **引入通道索引逻辑**：
    - 核心发送函数改为 `send_reply_to_linux_idx(..., int ch_idx)`，支持指定目标通道。
    - `send_reply_to_linux` 现在默认指向通道 0，确保了旧代码的兼容性。
- **添加前向声明**：解决了编译时由于函数定义顺序导致的 `used but never defined` 错误。

## 2. 多通道实现原理

多通道机制主要依赖于**内存分区映射**和**上下文传递**：

1.  **内存布局**：
    - 通道 0：起始地址 `ch0_tx_vaddr` / `ch0_rx_vaddr`。
    - 通道 1/2：通过 `CH1_OFFSET` 和 `CH2_OFFSET` 在基地址上进行偏移。
2.  **轮询与分发**：
    - `main` 循环通过 `for (int i = 0; i < 3; i++)` 遍历所有通道。
    - 当从某个 `g_rx_queues[i]` 获取消息后，会将索引 `i` 传递给处理函数。
3.  **响应回路**：
    - 处理函数（如 `service_encrypt_idx`）在完成计算后，调用 `send_reply_to_linux_idx` 并传入最初收到的 `ch_idx`，确保响应回到正确的发送队列。

## 3. 如何添加一个新通道（例如通道 3）

若要增加更多通道，请按照以下步骤操作：

### 第一步：修改宏定义
增加通道数量和偏移量定义：
```c
#define MAX_CHANNELS 4  // 原为 3
#define CH3_OFFSET   (3 * CH_SIZE) // 假设每个通道占用空间相同
```

### 第二步：扩展全局队列数组
```c
static volatile HyperampShmQueue *g_tx_queues[MAX_CHANNELS] = {NULL, ...};
static volatile HyperampShmQueue *g_rx_queues[MAX_CHANNELS] = {NULL, ...};
static volatile void *g_data_regions[MAX_CHANNELS] = {NULL, ...};
```

### 第三步：初始化内存映射
在 `main` 函数的地址计算部分增加新通道：
```c
g_tx_queues[3] = (volatile HyperampShmQueue *)(ch0_tx_vaddr + CH3_OFFSET);
g_rx_queues[3] = (volatile HyperampShmQueue *)(ch0_rx_vaddr + CH3_OFFSET);
g_data_regions[3] = (void *)(data_region_base + CH3_OFFSET);
```

### 第四步：调整主循环
确保主循环的 `for` 遍历上限包含了新通道：
```c
for (int i = 0; i < MAX_CHANNELS; i++) {
    // 轮询逻辑...
}
```

### 第五步：Linux 端同步
必须在 Linux 驱动侧（通常是 DTS 设备树或内核模块参数）同步配置相应的内存段，否则 seL4 访问该区域会导致内存故障。
