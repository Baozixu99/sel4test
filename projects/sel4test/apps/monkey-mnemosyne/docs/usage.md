# Monkey 共享内存用法

## 系统简介

Monkey 提供远程内存访问能力。采用服务器、客户端结构。

服务器拥有充足的内存。客户端通过网络协议连接到服务器，向服务器发起空间分配、内存写入、内存读取等请求操作。

按原设计，运行客户端的系统将自身内存（小）全部用作本地缓存，实际可用内存为所有可用服务器的内存总和。如此可实现在小内存设备上运行大内存应用负载。

理想情况下，客户端应控制宿主的内存页面管理；在宿主遇到缺页时，动态将本地内存映射到虚拟地址空间。其中，内存可以通过网络 Swap 到服务器，也可从服务器取回曾经 Swap 的页面数据，以实现无感内存拓展。

*页面管理和缺页处理能力目前在 seL4 端未实现。*

## 启动方法

先在某个 Linux 系统上启动服务端程序，再启动客户端程序（seL4）。

---

服务端启动方法：

```shell
./monkey-mnemosyne
```

你也可以通过命令行指定监听端口，如：

```shell
./monkey-mnemosyne --port 19070
```

测试时，建议开启后台运行：

```shell
./monkey-mnemosyne &
```

## 编译方法

### 服务器程序

建议使用已经编译好的 Monkey Mnemosyne 二进制。

适用于 aarch64 Linux，所有库皆已静态链接。

下载：[同济云盘](https://yunpan.tongji.edu.cn/link/AAC2788C5AD1194467B8199746E250C04F)

默认监听端口：10100

该版本支持的客户端 Key：

```
f578bd06-6f8e-42b3-8be9-860c7c645549
8370c1fe-d422-42d9-a261-05aed72313c3
6ba4d662-fa26-4c45-8627-af6c1328fc12
248ed556-d5d9-4488-9b32-bd8110867908
c41a230f-f695-41b1-8b02-204da9cf69d8
c9ff804a-6228-44da-b9af-d8f7e7fca108
3b574aa5-f501-4b25-85b8-fc4999d4f54e
34f47c96-2bc2-41dd-9072-309dc053a57d
b87028cb-a2f4-43f7-b81a-56ceeacd8433
ff8b74f4-1df8-47dc-adf8-b2f949fe8f13
30780b46-b2dd-4069-9d62-2fedc9d396bb
3770a74b-6ba3-4c8f-b27d-ff0a99557d35
d2765128-3ef5-442e-953d-257fcfa8e881
08e5aa35-54d6-4b5d-9fc9-4788d446db4f
80ed6bd7-e2d0-489a-94e6-f3aa1b698de4
07971453-1ea2-4cc5-82bd-fe758e1ad0e8
148ad0e6-6613-49a6-9e31-5ebc40bfe18d
fa98f25f-632a-447d-b474-6bb7c40e2e3a
3a283ad7-4109-4c33-83a0-0351a99b1dd2
c0660862-953f-49f7-9095-a66a01baba16
587bc2e9-541a-46e3-b3db-13f7fbab50fc
eb9df6e5-671a-4664-923f-fd9926ba2132
aed55551-83f4-42e2-b98d-8e9f53409e7d
0c318fb4-a451-4d36-aaf6-067f5852769d
29f859e8-2a52-4dcc-a313-7d490011fe44
808956be-bc2d-4488-a7d3-c43e9a08f25e
```

### 客户端编译方法

调整 `projects/sel4test/apps/hyperamp-server/src/channel_ch2.c` 里的宏定义：

```c
#define CH2_MNEMOSYNE_SERVER_IP     "192.168.137.2"
#define CH2_MNEMOSYNE_SERVER_PORT   10100
#define CH2_MNEMOSYNE_AUTH_KEY      "f578bd06-6f8e-42b3-8be9-860c7c645549"
```

其中，Auth Key 任选上方“客户端 Key”之一即可。若同时运行多个不同的客户端（如，多个不同的开发板连接到同一个服务端），应确保每个客户端使用不同的 Auth Key。

## 测试方案

### 解释

目前并没有找到较好的共享内存应用场景（暂时没跑通 seL4 缺页处理）。建议将本模块解释成“备用功能”，即可以允许小内存 seL4 系统访问到远超本地内存容量的物理内存（可访问容量为服务端物理内存总和）。

### 纯文本交换测试

见：`projects/sel4test/apps/hyperamp-server/src/channel_ch2.c` `send_plain_text`。

该测试中，客户端向服务端发送一个文本串。每次发送时，会改变“下一个字符”。如：

```
Round 1: Next char is: A. Hello from seL4!
Round 2: Next char is: B. Hello from seL4!
Round 3: Next char is: C. Hello from seL4!
...
```

服务端接收后，会立即向标准输出打印收到的文本，并将文本 ASCII 求和后发送回客户端。

该测试用于验证 Monkey 应用层协议通道正常工作。测试成功即表示分布式内存功能**可以**工作。

此外，客户端向服务端创建连接后，立即主动申请了一块内存（AI 这么做的。为简化测试，申请后暂不使用，但也懒得删申请逻辑）。本测试可以执行，说明前序“内存申请”工作已经成功。

