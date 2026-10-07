# FreeSWITCH 线程模型

## 一、范围与方法

本报告基于 FreeSWITCH 官方源码（GitHub signalwire/freeswitch）、官方开发者文档（developer.signalwire.com）、sofia-sip 参考文档以及社区 Issue 分析，系统性地拆解 FreeSWITCH 的并发与线程模型。内容覆盖核心架构、Session 线程模型、通道状态机、事件系统、mod_sofia/sofia-sip 线程模型、媒体处理线程、同步原语、内存管理以及高并发调优。

---

## 二、总体架构概览

FreeSWITCH **不是**单一事件循环（如 Node.js）架构，而是典型的**多线程并发系统**，其设计哲学可概括为：

**"Session 状态机 + 线程池 + Event 异步分发 + 模块自有工作线程"**

整体并发架构由四个隔离层组成：

```
                        FreeSWITCH Core
                             │
           ┌─────────────────┼────────────────┐
           │                 │                │
        Session            Event           Modules
           │                 │                │
     State Machine       Event Queue      Sofia / Conference
           │                 │            ESL / Media ...
           ▼                 ▼                ▼
     Session Thread      Event Workers    Module Threads
     / Thread Pool
           │
           ▼
     RTP / Codec / App / Dialplan
```

信号流的宏观路径为：

```
Signaling/Network → Core Session → Session Thread/Pool Worker → State Machine → Applications/Media
                                                                      ↕
Event Producers → Event Queue → Dispatch Thread(s) → Consumers
```

### 核心设计原则

1. **Session-centric（以会话为中心）**：每条通话 Leg 对应一个 `switch_core_session_t`，拥有独立的状态机执行上下文。
2. **并发粒度是 Leg 而非 Call**：一次 A↔B 桥接通话产生两个 Session，各自独立运行状态机。
3. **线程池复用**：现代 FreeSWITCH 默认使用 Session Thread Pool（`session-thread-pool=true`），避免每个 Session 创建独立 OS 线程。
4. **事件解耦**：事件子系统通过专用 dispatch 线程池实现生产者-消费者解耦。
5. **模块自治**：各端点/应用模块（如 mod_sofia、mod_conference）维护自己的线程和事件循环。

---

## 三、Session 线程模型（核心中的核心）

### 3.1 一 Leg 一 Session 的逻辑模型

```
             FreeSWITCH
        ┌─────────────────┐
A Leg → │ Session A       │
        │ Channel A       │
        │ State Machine A │
        └────────┬────────┘
                 │ bridge
        ┌────────▼────────┐
B Leg → │ Session B       │
        │ Channel B       │
        │ State Machine B │
        └─────────────────┘
```

**关键概念**：FreeSWITCH 中的 `session`、`channel`、`uuid` 都围绕 **Leg** 而非整通 Call 来理解。一次双腿通话意味着两个 Session 在并发执行。

### 3.2 Session 的创建与线程启动

核心调用链（源码入口：`src/switch_core_session.c`）：

```
呼叫进入（如 mod_sofia 收到 INVITE）
    ↓
switch_core_session_request()        ← 创建 session/channel
    ↓
switch_core_session_thread_launch()  ← 启动状态机线程
    或
switch_core_session_thread_pool_launch()  ← 线程池模式
    ↓
switch_core_session_thread()         ← session 线程入口
    ↓
switch_core_session_run()            ← 驱动状态机
    ↓
switch_core_state_machine.c          ← 各 CS_* 状态处理
    ↓
Dialplan / Application / Media / Hangup
```

`switch_core_session_thread_launch()` 的官方描述是："Launch the session thread (state machine)"。声明位于 `src/include/switch_core.h`。

### 3.3 传统模式 vs. 线程池模式

**传统模式**（`session-thread-pool=false`）：

```
1 Leg ≈ 1 Session ≈ 1 OS Thread (pthread)
```

每个 Session 创建一个独立的操作系统线程来运行状态机。在高并发场景下，线程创建/销毁开销显著。

**线程池模式**（`session-thread-pool=true`，当前默认）：

```
Producer
   │
   ▼
[ task/work queue ]
   │
   +───→ Worker 1 ── execute ──+
   +───→ Worker 2 ── execute ──+── wait/reuse
   +───→ Worker N ── execute ──+
```

工作由 `switch_thread_data_t` 表示，包含 worker 函数、上下文和关联的内存池。`switch_thread_pool_launch_thread()` 将工作提交到池中。Worker 线程（`switch_core_session_thread_pool_worker()`）等待队列中的工作项，执行完毕后回到等待状态，避免反复创建/销毁线程。

**配置项**（`switch.conf.xml`）：
- `session-thread-pool`：是否启用线程池（默认 `true`）
- `max-sessions`：最大 Session 数（默认 1000，生产环境需调高）
- `sessions-per-second`：每秒最大新建 Session 数（默认 30）
## 四、通道状态机（Channel State Machine）

### 4.1 状态流转

状态机实现位于 `src/switch_core_state_machine.c`，由 `switch_core_session_run()` 驱动。完整的状态流转：

```
CS_NEW
  │    ← Session 刚创建，无 STATE_MACRO 回调；端点负责推动到 CS_INIT
  ▼
CS_INIT
  │    ← STATE_MACRO(init) 执行，触发 CHANNEL_CREATE 事件
  │    ← 出站呼叫还触发 CHANNEL_ORIGINATE
  ▼
CS_ROUTING
  │    ← 路由/拨号计划查找阶段
  ▼
CS_EXECUTE
  │    ← 执行 dialplan action（bridge, playback, park 等）
  ▼
CS_EXCHANGE_MEDIA / CS_SOFT_EXECUTE
  │    ← 媒体交换/软执行（可选状态）
  ▼
CS_HANGUP
  │    ← 挂断处理
  ▼
CS_REPORTING
  │    ← CDR 等报告
  ▼
CS_DESTROY
  │    ← 资源回收、Session 销毁
  ▼
线程/Session 清理完成
```

### 4.2 状态处理的线程语义

**关键区分**：

- **`state`（请求状态）**：通过 `switch_channel_set_state()` 设置，**可从任意线程调用**。
- **`running_state`（运行状态）**：由 Session 状态机线程检测并更新，状态 handler 在此线程执行。

这意味着：
1. 外部线程可以请求状态转换（如 `set_state(CS_HANGUP)`）。
2. 实际的状态处理（`on_init`、`on_routing`、`on_execute`、`on_hangup` 等回调）在 Session 线程中执行。
3. `get_state()` → 判断 → `set_state()` 的序列**不是原子的**，存在竞态条件（GitHub Issue #1698 已记录此 race）。

### 4.3 Session 生命周期管理

`switch_core_session_thread()` 的完整生命周期：

```
Session 创建
  → switch_core_session_thread()
    → switch_core_session_run(session)    ← 驱动状态机
      → 状态机处理呼叫
      → CS_HANGUP / CS_REPORTING
    → 等待外部引用/锁释放              ← "Locked, Waiting on external entities"
    → CS_DESTROY
    → Session 清理/销毁
```

`switch_core_session_locate(uuid)` 获取一个 Session 的读锁，保护 Session 生命周期。调用方**必须**配对调用 `switch_core_session_rwunlock()` 释放锁。如果忘记释放，Session 会在 hangup 后仍无法销毁，日志中出现 "Locked, Waiting on external entities"。

---

## 五、事件系统（Event System）

### 5.1 生产者-消费者架构

FreeSWITCH 的事件分发是典型的**生产者-消费者**模式：

```
Producer Thread
    │
    ▼
switch_event_fire()     ← 创建并发射事件
    │
    ▼
[ Event Queue ]         ← 内部队列
    │
    ▼
Dispatch Thread(s)      ← 多个分发线程
    │
    ▼
switch_event_deliver()  ← 投递给注册的消费者
    │
    ▼
Bound Callbacks         ← switch_event_bind() 注册的回调
```

### 5.2 配置与线程控制

关键配置项（`switch.conf.xml` 的 `core-db-settings`/`core-settings`）：

- **`events-use-dispatch=true`**（默认）：启用事件队列模式，事件不在调用方线程直接消费，而是队列化后由 dispatch 线程处理。
- **`initial-event-threads`**：控制启动时的事件分发线程数。

### 5.3 线程安全注意事项

1. **事件回调可能并发执行**：多个 dispatch 线程意味着两个事件的回调可能在不同线程同时运行。
2. **事件所有权转移**：`switch_event_fire()` 成功后，FreeSWITCH 接管事件对象，调用方不应继续使用该 `switch_event_t *`。
3. **避免阻塞回调**：event callback 中的阻塞操作会占用 dispatch 线程。推荐模式：

```
FreeSWITCH callback → 私有有界队列 → Worker 线程 → DB/HTTP/...
```

FreeSWITCH 自身的 `mod_json_cdr` 就采用这种后台队列模式。
## 六、mod_sofia 与 sofia-sip 线程模型

### 6.1 sofia-sip 的 su_root_t 事件循环

sofia-sip 的核心是一个 **Reactor 模式**的事件循环，围绕 `su_root_t` 对象构建：

```
Thread → su_root_t/Event Loop → poll/epoll → I/O/Timer/Message Ready → Callback → Repeat
```

典型初始化：

```c
su_root_create()
    → nua_create(root, callback, ...)
    → su_root_run(root)     // 阻塞在事件循环
```

**关键特性**：

1. **Reactor 模式**：注册事件源 → 等待就绪 → 分发回调，而非每个 SIP dialog 一个线程。
2. **NUA 回调在事件循环线程执行**：调用 `su_root_run()` 的线程就是 NUA 事件的处理线程。
3. **协议栈线程分离**：NUA 可以在单独的栈线程中处理协议工作，与应用的 `su_root` task 之间通过异步消息传递通信。
4. **Task 模型**：多个 task 可共享一个事件循环线程，而 clone task 可以选择在独立线程执行。`su_root_threading()` 控制此行为。
5. **可嵌入**：`su_root_step()` 替代 `su_root_run()` 可实现单步执行，方便集成到其他事件循环。

**并发模型示意**：

```
Application Thread → su_root Event Loop → NUA Callback
                           ↕ messages
                      NUA Stack Thread
```

### 6.2 mod_sofia 的线程架构

mod_sofia 是 FreeSWITCH 最核心的端点模块，连接 sofia-sip 和 FreeSWITCH Core。

```
Network (UDP/TCP/TLS)
    │
    ▼
Sofia tport/NTA           ← 传输层/事务层
    │
    ▼
NUA Event Loop            ← sofia-sip 用户代理引擎
    │
    ▼
mod_sofia Callback        ← NUA 回调，处理 SIP 事件
    │
    ├──→ FS Session Queue / Session Thread   ← 创建/管理 Session
    │
    └──→ msg_queue_thread[]                  ← mod_sofia 内部消息队列线程
```

**关键设计点**：

1. **每个 SIP Profile 一个独立上下文**：每个 Sofia SIP Profile 拥有自己的 NUA/su_root 事件循环。Profile 代表一个独立的 SIP UA/监听器。
2. **不是每个 SIP 对话一个线程**：SIP 信令在对应 Profile 的事件循环中**事件驱动、近似串行**处理。
3. **消息队列工作线程**：`mod_sofia.h` 中定义了 `msg_queue_thread[]`，说明模块内部并非严格单线程。
4. **工作分离**：耗时的呼叫处理不在 Sofia 事件循环中执行，而是分发到 FreeSWITCH 的 Session 线程。

**`nua_handle_t`** 代表一个 SIP 操作/对话关系，它本身**不是**线程。不要对 NUA handle 进行任意的并发访问——应将其视为事件循环所有的对象。

### 6.3 从 SIP INVITE 到 Session 的完整路径

```
SIP INVITE (UDP)
    │
    ▼
Sofia tport 接收
    │
    ▼
NTA (事务层) 处理
    │
    ▼
NUA 状态机 → nua_i_invite 回调
    │
    ▼
mod_sofia: sofia_handle_sip_i_invite()
    │
    ├── switch_core_session_request()      ← 创建 FS Session
    ├── 设置 tech_pvt (Sofia 私有数据)
    └── switch_core_session_thread_launch() ← 启动 Session 线程
         │
         ▼
    switch_core_session_run()
         │
         ▼
    CS_NEW → CS_INIT → CS_ROUTING → ...
```

---

## 七、媒体处理线程

### 7.1 RTP/媒体处理路径

```
UDP/RTP → switch_rtp.c → Jitter Buffer/Packet Handling
    → Codec Decode → PCM Frame → Core Media Processing
    → Codec Encode → switch_rtp.c → RTP/UDP
```

核心源码文件：
- `src/switch_rtp.c`：RTP/RTCP 收发、包排序、时序、DTLS/SRTP、统计。
- `src/switch_core_media.c`：媒体引擎设置、Codec 协商、RTP Session 配置。

### 7.2 Media Bug 回调的线程语义

Media Bug 是 FreeSWITCH 访问解码后音频帧的主要机制（录音、STT、分析等）。

**关键规则**：Media Bug 回调在当前处理媒体路径的线程**同步执行**，不会自动获得独立的 Worker 线程。

对于 `READ_REPLACE`/`WRITE_REPLACE` 类型的回调，必须视为在**实时媒体路径**上执行。因此：

- ❌ 不要在回调中阻塞、sleep、执行网络 I/O 或耗时的 DSP 运算
- ✅ 将音频数据拷贝/入队到线程安全的 buffer，在自己的 Worker 线程中处理
- ⚠️ Codec/Session 操作需谨慎：FreeSWITCH 使用 Codec 读写锁，锁序错误可导致死锁

### 7.3 异步 IVR 操作

`switch_ivr_async.c` 实现了可以异步运行的 IVR/媒体操作。注意 "async" 并不意味着每个函数都创建新线程：

- **Media Bug 方式**：附加到 Session，随帧处理时被调用（如 DTMF 检测）。
- **独立 Worker 线程**：某些操作确实创建线程（如 `recording_thread`）。
- **需要逐个函数确认**其线程模型。
## 八、同步原语与线程安全

### 8.1 核心同步原语

FreeSWITCH 基于 APR（Apache Portable Runtime）封装了一套跨平台的同步原语：

| 原语 | 类型 | 用途 |
|------|------|------|
| `switch_mutex_t` | 互斥锁 | 排他访问，保护共享可变状态 |
| `switch_thread_rwlock_t` | 读写锁 | 多读单写场景（如 Session 引用计数） |
| `switch_queue_t` | 线程安全队列 | 生产者-消费者模式 |

**使用模式**：

```c
// 互斥锁——排他访问
switch_mutex_t *mutex;
switch_mutex_init(&mutex, SWITCH_MUTEX_NESTED, pool);
switch_mutex_lock(mutex);
/* 修改共享状态 */
switch_mutex_unlock(mutex);

// 读写锁——多读单写
switch_thread_rwlock_t *rwlock;
switch_thread_rwlock_create(&rwlock, pool);
switch_thread_rwlock_rdlock(rwlock);   // 读者
switch_thread_rwlock_wrlock(rwlock);   // 写者
switch_thread_rwlock_unlock(rwlock);
```

选择 `switch_mutex_t` 当访问需要简单序列化时；选择 `switch_thread_rwlock_t` 当读操作频繁且写操作稀少时。

### 8.2 Session 锁机制

**这是最重要的线程安全模式之一**：

```
switch_core_session_locate(uuid)
    → 获取 Session 读锁
    → 保护 Session 生命周期
    → 在另一个线程安全地访问 Session
    → ...操作...
    → switch_core_session_rwunlock(session)
    → 释放读锁
```

`switch_core_session_locate()` 不仅查找 Session，还**获取读锁保护其生命周期**。这意味着只要持有锁，Session 就不会被销毁。必须配对释放，否则导致 Session 泄漏。

### 8.3 常见并发陷阱

1. **状态检查-设置竞态**：`get_state()` → 判断 → `set_state()` 不是原子操作，并发修改可能导致状态不一致。

2. **锁序死锁**：持有自定义 mutex 的同时调用 FreeSWITCH API（如 `switch_core_session_receive_message()`），endpoint 回调可能获取 Codec/Media/Sofia 锁，形成锁序反转死锁。FreeSWITCH Issue #695、#2290 均记录了此类问题。

3. **`switch_core_session_receive_message()` 的线程语义**：可从非 Session 线程调用，但**直接在调用方线程执行** endpoint 的 `receive_message()` 回调，并非异步投递。需持有有效的 Session 引用/读锁。

---

## 九、内存管理模型

### 9.1 APR 内存池

FreeSWITCH 基于 APR 的层次化内存池（`switch_memory_pool_t` / `fspr_pool_t`）：

- 每个 `switch_core_session_t` 拥有独立的内存池 `session->pool`。
- `switch_core_session_alloc(session, size)` 从 Session 池分配并清零。
- 分配的内存**不需要单独 free**，随 Session 池一起回收。
- **禁止**用 Session 池分配需要超越 Session 生命周期的数据。

```c
void *p = switch_core_session_alloc(session, size);
char *s = switch_core_session_strdup(session, "hello");
switch_memory_pool_t *pool = switch_core_session_get_pool(session);
```

### 9.2 内存池与线程的关系

- 内存池是**分配生命周期管理工具**，不是线程池。
- 每个 Session 的池与 Session 生命周期绑定，销毁时一次性释放。
- 全局/模块级数据应使用模块自己的池或 Core 提供的永久池。

---

## 十、其他关键线程子系统

### 10.1 SQL 线程

`switch_core_sqldb.c` 使用**专用 SQL 线程**减少数据库竞争：

```
Session/Module Threads → SQL Queue → SQL Thread → Database
```

- SQLite 后端：写操作序列化，SQL 队列避免多个 Session 线程竞争写锁。
- ODBC/PostgreSQL/MySQL：可使用连接池，并发度更高，但数据库级死锁仍可能发生。
- SQL 线程是序列化/批量化基础设施，**不保证**所有数据库访问都是单线程的。

### 10.2 调度器线程

`switch_scheduler` 使用调度器+Worker 线程模型：

```
Scheduler Clock → 发现到期任务 → Queue → Worker Thread(s) → Callback
```

与媒体/Core 定时器（`switch_time.c`）是不同的机制。

### 10.3 模块运行时线程

定义了 `SWITCH_MODULE_RUNTIME_FUNCTION` 的模块，由 FreeSWITCH 通过 `switch_core_launch_thread()` 启动运行时线程：

```c
while (status != SWITCH_STATUS_TERM && !module->shutting_down) {
    status = module->switch_module_runtime();
}
```

模块内部额外创建的线程需自行管理生命周期——在 `shutdown` 回调中 signal 并 join 所有线程。

---

## 十一、高并发调优要点

### 11.1 万级并发的关键配置

| 配置项 | 默认值 | 生产建议 |
|--------|--------|----------|
| `max-sessions` | 1000 | 高于实际需求（桥接通话算双 Session） |
| `sessions-per-second` | 30 | 按压测结果设置 |
| `session-thread-pool` | true | 保持开启 |
| `initial-event-threads` | - | 适当增加事件分发线程 |
| `events-use-dispatch` | true | 保持开启 |

### 11.2 系统层面

- 提高 `nofile` 限制（文件描述符）
- 调整 UDP/socket buffer 和 RTP 端口范围
- 高时钟频率 CPU + 足够核心数
- 禁用生产环境的 debug/SIP trace 日志

### 11.3 架构层面

- 避免转码（`bypass_media` 或 `proxy_media`）
- 万级通话建议多节点 + SIP 代理/负载均衡器
- 注意"通话数"vs"Session 数"：10,000 并发桥接通话 ≈ 20,000 Sessions
- 使用 SIPp 进行压测，监控 CPU、内存、丢包、RTP jitter、FD 数、CPS

---

## 十二、源码阅读路径建议

按以下顺序阅读 FreeSWITCH 源码，逐步建立并发模型的全景理解：

```
1. switch_core.c              ← Core 启动、线程创建封装
2. switch_core_session.c      ← Session 创建/销毁/线程启动（最核心）
3. switch_core_state_machine.c ← 状态机驱动
4. switch_channel.c           ← Channel 状态/变量/标志
5. switch_core_media.c        ← 媒体引擎/RTP/Codec
6. switch_event.c             ← 事件队列/异步分发
7. switch_rtp.c               ← RTP 收发
8. mod_sofia/mod_sofia.c      ← SIP 端点模块
```

在每个文件中关注：**"谁拥有这个对象？哪些线程可以访问它？什么机制保护它在另一个线程访问时不被销毁？"**

---

## 十三、总结

FreeSWITCH 的并发模型可以用一张图概括：

```
┌─────────────────────────────────────────────────────────────┐
│                    FreeSWITCH Process                        │
│                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │ Sofia Profile│  │ Sofia Profile│  │ Other Modules       │ │
│  │ su_root loop │  │ su_root loop │  │ (Conference, ESL..) │ │
│  └──────┬───────┘  └──────┬───────┘  └──────┬──────────────┘ │
│         │                 │                 │               │
│         ▼                 ▼                 ▼               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │            Session Thread Pool                       │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐             │   │
│  │  │ Worker 1 │ │ Worker 2 │ │ Worker N │  ...        │   │
│  │  │Session A │ │Session B │ │Session C │             │   │
│  │  │StateMach │ │StateMach │ │StateMach │             │   │
│  │  └──────────┘ └──────────┘ └──────────┘             │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │          Event Dispatch Thread Pool                  │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐             │   │
│  │  │Dispatch 1│ │Dispatch 2│ │Dispatch N│  ...        │   │
│  │  └──────────┘ └──────────┘ └──────────┘             │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌───────────┐ ┌───────────┐ ┌────────────┐                │
│  │ SQL Thread│ │ Scheduler │ │ Heartbeat  │                │
│  └───────────┘ └───────────┘ └────────────┘                │
└─────────────────────────────────────────────────────────────┘
```

**三句话总结**：

1. **Session 是并发的基本单位**——每条 Leg 一个 Session，拥有独立状态机，由线程池 Worker 驱动。
2. **事件系统实现解耦**——生产者-消费者模式，多个 Dispatch 线程异步投递，避免阻塞呼叫处理。
3. **SIP 信令是事件驱动的**——mod_sofia 通过 sofia-sip 的 Reactor 事件循环处理信令，然后将工作分发到 Session 线程，形成"事件驱动信令 + 多线程呼叫处理"的混合架构。

---

**参考来源**：
- FreeSWITCH Source: github.com/signalwire/freeswitch (switch_core_session.c, switch_core_state_machine.c, switch_event.c, switch_core.h, mod_sofia.h)
- FreeSWITCH Developer Docs: developer.signalwire.com/freeswitch/configuration/core-settings/
- Sofia-SIP Reference: sofia-sip.sourceforge.net/refdocs/nua/, sofia-sip.sourceforge.net/refdocs/su/group__su__wait.html
- FreeSWITCH Issues: #76, #695, #1698, #2063, #2290, #2389, #2545, #2593, #2879
