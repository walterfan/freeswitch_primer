# FreeSWITCH 内存模型与内存管理
---

## 一、内存模型总览

FreeSWITCH 的内存管理架构建立在 **APR（Apache Portable Runtime）风格的内存池**之上，其核心抽象类型为 `switch_memory_pool_t`，底层实现通过 FreeSWITCH 自带的 FSPR（FreeSWITCH APR）层（`fspr_pool_*` 系列函数）完成。这一设计的核心思想是**基于生命周期的内存管理**，而非传统的逐个 `malloc/free` 模式。

### 1.1 核心设计原则

内存池的根本理念是：**共享同一生命周期的对象，分配到同一个池中**。当池被销毁时，该池中的所有分配将被一次性批量回收，无需逐一释放。这对于电话服务器而言意义重大——当一通电话（session）结束时，其关联的所有内存通过销毁 session pool 即可全部清理，避免了大量复杂的逐对象析构代码。

### 1.2 内存池层次结构

```
Core / Master Pool（核心主池）
│   生命周期 = FreeSWITCH 进程生命周期
│
├── Module Pool（模块池）
│   生命周期 = 模块加载到卸载
│
├── Session Pool（会话池，每通呼叫一个）
│   生命周期 = 呼叫/会话的整个持续时间
│   │
│   ├── switch_core_session_t（会话结构体）
│   ├── switch_channel_t（通道结构体）
│   ├── 通道变量（channel variables）
│   ├── 编解码器 / 媒体结构体
│   └── 其他会话级分配
│
└── 其他子系统池
    例如：事件系统、数据库连接等
```

**关键认知**：`switch_channel_t` 并没有独立的"通道池"，它从属于 session，其生命周期与 session pool 绑定。所有通过 `switch_core_session_alloc()` 分配的内存都归属于该 session 的池。

### 1.3 内存池的创建与销毁实现

在 `src/switch_core_memory.c` 中，池的创建和销毁通过宏捕获源代码位置信息以便调试：

**创建**：`switch_core_new_memory_pool(&pool)` 展开为 `switch_core_perform_new_memory_pool(&pool, __FILE__, __SWITCH_FUNC__, __LINE__)`，内部调用 `fspr_pool_create(pool, NULL)` 创建 APR 池，并为池打上创建位置标记（`file:line`）。

**销毁**：`switch_core_destroy_memory_pool(&pool)` 先将指针置 NULL（`tmp_pool = *pool; *pool = NULL;`），然后通常将池排入内存池工作线程的队列进行**异步销毁**，以避免在延迟敏感路径上执行耗时的销毁操作。如果工作线程未运行或入队失败，则直接调用 `fspr_pool_destroy(tmp_pool)` 同步销毁。编译选项 `INSTANTLY_DESTROY_POOLS` 可强制同步销毁。

---

## 二、内存管理 API 函数库

FreeSWITCH 提供两大类内存分配方式：**池分配（Pool Allocation）**和**堆分配（Heap Allocation）**，必须严格区分使用。

### 2.1 池分配 API（生命周期跟随池）

| 函数 / 宏 | 说明 | 释放方式 |
|---|---|---|
| `switch_core_alloc(pool, size)` | 从指定池分配 `size` 字节，返回零初始化的内存 | 池销毁时自动释放 |
| `switch_core_strdup(pool, str)` | 在池中复制字符串 | 池销毁时自动释放 |
| `switch_core_sprintf(pool, fmt, ...)` | 格式化字符串分配到池中 | 池销毁时自动释放 |
| `switch_core_session_alloc(session, size)` | 从 session 的池分配，适合呼叫生命周期数据 | session 销毁时自动释放 |
| `switch_core_session_strdup(session, str)` | 在 session 池中复制字符串 | session 销毁时自动释放 |
| `switch_core_permanent_alloc(size)` | 从核心主池分配，直到 FreeSWITCH 进程退出才释放 | 进程退出时释放 |
| `switch_core_new_memory_pool(&pool)` | 创建新的子内存池 | 需手动调用 `switch_core_destroy_memory_pool` |
| `switch_core_destroy_memory_pool(&pool)` | 销毁池及其所有分配 | — |

**重要特性**：`switch_core_alloc()` 底层调用 `fspr_palloc` 后会将内存清零，因此返回的内存始终是零初始化的。池中的单个分配**不能单独释放**——只能整池销毁。

### 2.2 堆分配 API（需手动释放）

| 函数 / 宏 | 说明 | 释放方式 |
|---|---|---|
| `switch_must_malloc(size)` | 带检查的 `malloc`，分配失败时中止 | `switch_safe_free()` 或 `free()` |
| `switch_must_realloc(ptr, size)` | 带检查的 `realloc` | `switch_safe_free()` 或 `free()` |
| `switch_zmalloc(ptr, len)` | 零初始化分配（`calloc(1, len)`），失败时中止 | `switch_safe_free()` |
| `switch_calloc(ptr, count, size)` | `calloc` 包装 | `switch_safe_free()` |
| `switch_realloc(ptr, size)` | `realloc` 包装 | `switch_safe_free()` |
| `switch_safe_free(ptr)` | 安全释放：非 NULL 时 `free`，然后置 NULL | — |

**典型堆分配模式**：

```c
char *buf = NULL;
switch_zmalloc(buf, 1024);
/* 使用 buf */
switch_safe_free(buf);
```

### 2.3 缓冲区与事件内存管理

**`switch_buffer_t`**（缓冲区）：

- 通过 `switch_buffer_create_dynamic()` 创建的动态缓冲区拥有堆存储，必须用 `switch_buffer_destroy()` 释放。
- 池支持的缓冲区跟随池的生命周期，无需单独释放。

**`switch_event_t`**（事件）：

- `switch_event_create()` / `switch_event_create_subclass()` 创建的事件由调用者拥有，需调用 `switch_event_destroy(&event)` 释放——除非所有权已通过 fire 等 API 转移。
- `switch_event_get_header()` / `switch_event_get_body()` 返回的是**借用指针**，不可单独释放。
- `switch_event_add_header_string()` 会复制传入的字符串。
- 序列化接口返回的 `char *`（如 JSON/XML 序列化结果）需要调用者负责释放。

### 2.4 线程安全相关

```c
switch_mutex_t *mutex;
switch_mutex_init(&mutex, SWITCH_MUTEX_NESTED, pool);

switch_mutex_lock(mutex);
/* 访问共享状态 */
switch_mutex_unlock(mutex);
```

**关键要点**：将 `pool` 传入 `switch_mutex_init()` 只控制 mutex 自身的生命周期，并**不会**让该池中的其他分配变为线程安全。必须在访问共享数据时显式加锁。`SWITCH_MUTEX_NESTED` 允许同一线程递归获取锁；`SWITCH_MUTEX_UNNESTED` 则不允许。同时必须确保池的生命周期超过所有可能访问该锁或池中对象的线程。

---

## 三、编程规范

### 3.1 按生命周期选择分配方式（核心准则）

| 数据的预期生命周期 | 应使用的分配方式 |
|---|---|
| 临时 / 短暂（函数内或循环内） | `switch_must_malloc` / `switch_zmalloc` + `switch_safe_free` |
| 呼叫/会话生命周期 | `switch_core_session_alloc(session, ...)` |
| 模块生命周期 | `switch_core_alloc(module_pool, ...)` 或模块自有池 |
| 进程生命周期 | `switch_core_permanent_alloc(...)` |
| 需要精确控制的有限生命周期 | `switch_core_new_memory_pool` + `switch_core_alloc` + `switch_core_destroy_memory_pool` |

### 3.2 绝对不可混用分配器

- **绝不** 对 `switch_core_alloc()` / `switch_core_session_alloc()` 返回的内存调用 `free()` 或 `switch_safe_free()`。
- **绝不** 对 `malloc` / `switch_must_malloc` 分配的内存期望"池销毁时自动清理"。
- 外部库分配的内存需使用对应库的析构函数释放，池清理不会覆盖外部分配。

### 3.3 Session 引用规范

- `switch_core_session_locate()` 会锁定 session（增加引用计数），每次成功调用**必须**配对 `switch_core_session_rwunlock()`。
- 遗漏 unlock 会导致 session 无法正常销毁，从而造成内存泄漏。

---

## 四、最佳实践

### 4.1 池分配最佳实践

1. **避免在 session pool 中进行重复/临时分配**。由于池中的单个分配无法释放，在长时间会话中反复通过 `switch_core_session_alloc` 分配临时对象会导致内存持续增长直到 session 结束。对于此类场景，应使用堆分配（`switch_must_malloc` + `switch_safe_free`）。

2. **为有限生命周期的资源创建独立子池**。如果某些资源的生命周期短于 session 但长于单次函数调用，创建独立的 `switch_memory_pool_t` 子池，在资源不再需要时销毁子池。

3. **仅将 `switch_core_permanent_alloc` 用于真正的进程级全局数据**。此类分配在整个 FreeSWITCH 运行期间无法释放。

### 4.2 堆分配最佳实践

1. **始终使用 `switch_safe_free` 而非裸 `free`**，它会自动检查 NULL 并置指针为 NULL，防止 double-free。

2. **采用统一的清理路径**：在函数中使用 `cleanup:` 标签模式，在入口将所有指针初始化为 NULL，出错时跳转到统一清理路径。

```c
char *buf = NULL;
switch_event_t *event = NULL;

switch_zmalloc(buf, 1024);
if (switch_event_create(&event, SWITCH_EVENT_CUSTOM) != SWITCH_STATUS_SUCCESS) {
    goto cleanup;
}
/* ... 使用 buf 和 event ... */

cleanup:
    switch_safe_free(buf);
    if (event) switch_event_destroy(&event);
```

### 4.3 模块开发最佳实践

1. **模块全局资源（互斥锁、外部库对象、套接字、显式分配的缓冲区）必须在模块 unload 函数中明确释放**。

2. **事件的所有权转移必须清晰**：如果事件通过 fire 等 API 发送后所有权已转移，调用者不应再 destroy。

3. **序列化产生的字符串需要调用者释放**：例如事件 JSON/XML 序列化的结果。

### 4.4 线程安全最佳实践

1. **共享数据必须加锁**，即使数据和锁在同一个池中分配。

2. **确保池的生命周期 > 所有引用线程的生命周期**。如果一个线程仍在使用池中的对象，该池不能被销毁。

3. **Session 跨线程使用时必须通过 `switch_core_session_locate` / `switch_core_session_rwunlock` 管理引用**。

---

## 五、常见错误

### 5.1 池/堆混用（Pool/Heap Mismatch）

**错误示例**：

```c
/* 错误！pool 分配的内存不能 free */
char *str = switch_core_session_strdup(session, "hello");
free(str);  // ❌ 崩溃或未定义行为
```

**正确做法**：不需要手动释放，让 session 销毁时自动回收。

### 5.2 池销毁后使用（Use-After-Pool-Destroy）

**场景**：异步回调或后台线程持有指向 session pool 中数据的指针，但 session 已经结束并销毁了池。

```c
/* 错误！session 结束后，data 指向已释放内存 */
static void async_callback(void *data) {
    my_data_t *d = (my_data_t *)data;  // ❌ session pool 可能已销毁
    printf("%s", d->name);
}
```

**正确做法**：对需要在 session 生命周期之外使用的数据，使用堆分配并复制。

### 5.3 Double Free

**场景**：两个清理路径都释放了同一个堆对象，或手动清理与自动清理冲突。

```c
/* 错误！两次释放 */
free(buf);
/* ... 其他代码 ... */
free(buf);  // ❌ double free
```

**正确做法**：使用 `switch_safe_free(buf)` 自动置 NULL。

### 5.4 Session Pool 中的临时分配膨胀

```c
/* 错误！循环中持续向 session pool 分配临时内存 */
for (int i = 0; i < 100000; i++) {
    char *tmp = switch_core_session_alloc(session, 1024);
    /* tmp 永远无法释放，直到 session 结束 */
}
```

**正确做法**：对临时数据使用堆分配。

### 5.5 忘记 Session Unlock

```c
switch_core_session_t *other = switch_core_session_locate(uuid);
if (other) {
    /* 做一些事情 */
    return;  // ❌ 忘记 unlock！session 将永远无法销毁
}
```

**正确做法**：必须配对 `switch_core_session_rwunlock(other)`。

### 5.6 错误的生命周期池

```c
/* 错误！全局/长生命周期数据放在 session pool 中 */
globals.config = switch_core_session_strdup(session, config_str);
/* session 结束后 globals.config 变成悬垂指针 */
```

**正确做法**：全局数据应使用模块池或 `switch_core_permanent_alloc`。

---

## 六、内存调试与检测工具

### 6.1 Valgrind Memcheck

```bash
valgrind \
  --tool=memcheck \
  --leak-check=full \
  --show-leak-kinds=all \
  --track-origins=yes \
  --num-callers=30 \
  --log-file=/tmp/freeswitch.valgrind \
  /usr/local/freeswitch/bin/freeswitch -nf
```

分析重点：`definitely lost`（确定泄漏）和重复出现的分配栈。`still reachable` 通常不是泄漏——FreeSWITCH 及其库会有意保留池/缓存直到关闭。Valgrind 运行速度约为正常的 1/20~1/30，建议使用最小化配置和模块复现问题。

### 6.2 AddressSanitizer (ASan)

```bash
./bootstrap.sh -j

CFLAGS="-O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined" \
CXXFLAGS="-O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined" \
LDFLAGS="-fsanitize=address,undefined" \
./configure

make -j$(nproc)
```

运行时：

```bash
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:symbolize=1"
./freeswitch -nf
```

ASan 是区分 double-free、heap-use-after-free 和池生命周期错误的最快方法，性能开销远小于 Valgrind（约 2x）。

### 6.3 RSS 监控

对于长时间运行的内存增长，在反复执行相同呼叫/测试场景时对比 RSS 值：

```bash
while true; do
    ps -o rss,vsz -p $(pidof freeswitch)
    sleep 60
done
```

---

## 七、检查清单（Checklist）

### 代码审查检查清单

- [ ] **分配器匹配**：每处 `switch_core_alloc` / `switch_core_session_alloc` 返回的指针，确认没有对其调用 `free()` 或 `switch_safe_free()`
- [ ] **堆分配配对**：每处 `switch_must_malloc` / `switch_zmalloc` / `malloc`，确认有对应的 `switch_safe_free()` / `free()` 在所有退出路径上
- [ ] **Session unlock 配对**：每处 `switch_core_session_locate()`，确认所有分支（包括错误路径）都有 `switch_core_session_rwunlock()`
- [ ] **无临时数据池分配**：确认没有在循环或频繁调用路径中使用 `switch_core_session_alloc` 分配临时/一次性数据
- [ ] **生命周期匹配**：确认分配的池与数据的预期使用周期匹配（session 数据用 session pool、全局数据用模块池或 permanent_alloc）
- [ ] **跨线程数据安全**：确认跨线程共享的数据有 mutex 保护，且池的生命周期长于所有引用线程
- [ ] **异步回调数据所有权**：确认异步回调/后台线程使用的数据不依赖于可能先被销毁的 session pool
- [ ] **事件所有权**：每个 `switch_event_create` 有对应的 `switch_event_destroy`（除非所有权已转移给 fire 等 API）
- [ ] **缓冲区释放**：`switch_buffer_create_dynamic` 创建的缓冲区有对应的 `switch_buffer_destroy`
- [ ] **序列化结果释放**：事件序列化（JSON/XML）返回的字符串已被正确释放
- [ ] **外部库资源清理**：模块 unload 函数中释放了所有外部库分配的资源（内存、句柄、连接等）
- [ ] **指针初始化**：所有指针在声明时初始化为 NULL，便于统一清理路径判断
- [ ] **使用 `switch_safe_free` 而非裸 `free`**：防止 double-free 和悬垂指针

### 模块上线检查清单

- [ ] 在 ASan 构建下完整运行模块功能测试，无错误报告
- [ ] 在 Valgrind 下运行关键场景，`definitely lost` 为零
- [ ] 长时间压力测试下 RSS 稳定（无持续增长趋势）
- [ ] 模块 load/unload 循环测试无内存泄漏
- [ ] 高并发场景下无崩溃（验证线程安全）

---

## 八、参考来源

1. FreeSWITCH 源码 `src/switch_core_memory.c` — 内存池创建/销毁实现 ([github.com/signalwire/freeswitch](https://github.com/signalwire/freeswitch/blob/master/src/switch_core_memory.c))
2. FreeSWITCH 头文件 `src/include/switch_core.h` — 内存 API 声明 ([github.com/signalwire/freeswitch](https://github.com/signalwire/freeswitch/blob/master/src/include/switch_core.h))
3. FreeSWITCH 头文件 `src/include/switch_utils.h` — `switch_safe_free` 等工具宏 ([github.com/signalwire/freeswitch](https://github.com/signalwire/freeswitch/blob/master/src/include/switch_utils.h))
4. SignalWire 开发者文档 — 模块开发指南 ([developer.signalwire.com](https://developer.signalwire.com/freeswitch/FreeSWITCH-Explained/Community/Contributing-Code/Creating-New-Modules/))
5. SignalWire 开发者文档 — 事件模型 ([developer.signalwire.com](https://developer.signalwire.com/freeswitch/programming/event-model/))
6. Apache APR 池文档 ([apr.apache.org](https://apr.apache.org/docs/apr/trunk/group__apr__pools.html))
7. Valgrind Memcheck 手册 ([valgrind.org](https://valgrind.org/docs/manual/mc-manual.html))
8. FreeSWITCH Issue #1475 — Double-free 问题 ([github.com/signalwire/freeswitch/issues/1475](https://github.com/signalwire/freeswitch/issues/1475))
9. FreeSWITCH Issue #2344 — mod_verto 正则表达式泄漏 ([github.com/signalwire/freeswitch/issues/2344](https://github.com/signalwire/freeswitch/issues/2344))
