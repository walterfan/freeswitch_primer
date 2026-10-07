# 17. Sofia-SIP NUA：先建立正确的心智模型

<!-- maintained-by: human+ai -->

NUA（Sofia SIP User Agent）是 Sofia-SIP 面向应用的高层用户代理模块。它把
SIP 事务、对话、注册、订阅、发布和基本 SDP Offer/Answer 组合成一组异步
API；底层事务主要由 NTA 处理，传输由 tport 处理。[官方 NUA 概览](https://sofia-sip.sourceforge.net/refdocs/nua/index.html)

## 1. NUA 在 Sofia-SIP 中的位置

```text
应用程序
   |
   | nua_invite / nua_register / nua_message / nua_subscribe ...
   v
 NUA：高层用户代理、对话、呼叫状态、SDP 协商
   |
   v
 NTA：SIP transaction
   |
   v
 tport：UDP / TCP / TLS 等传输

 NUA <-> SOA：媒体会话和 SDP Offer/Answer
```

可以把 NUA 理解为“帮应用管理 SIP 状态机的异步控制器”：

- 应用决定要做什么：呼叫、接听、挂断、注册或订阅。
- NUA 负责把动作转换为 SIP 报文和事务。
- NUA 通过 callback 把响应、入站请求、呼叫状态和 SDP 结果交还应用。
- 应用负责业务策略，例如是否接听、如何处理 MESSAGE body、是否允许订阅。

NUA 可以创建终端、网关或 MCU 类型的 SIP 用户代理；它隐藏了很多低层信令
细节，但仍允许应用通过 tags 控制 SIP 和媒体参数。

## 2. 四个必须理解的对象

| 对象 | 类型 | 作用 | 生命周期 |
|---|---|---|---|
| Root | `su_root_t *` | 事件循环、定时器、异步消息分发 | 先创建，最后销毁 |
| Agent | `nua_t *` | 一个 SIP UA stack，包含传输和全局参数 | `nua_create()` 到 `nua_destroy()` |
| Handle | `nua_handle_t *` | 一次呼叫、注册、订阅或简单 SIP 操作的上下文 | `nua_handle()` 到 `nua_handle_destroy()` |
| Magic | `void *` 类型上下文 | 把应用自己的上下文带回 callback | 由应用管理 |

### 2.1 Root：事件循环

NUA 不会替应用主动运行事件循环。应用必须创建 root，并调用：

```c
su_root_t *root = su_root_create(app_context);

/* 阻塞运行，直到 su_root_break(root) */
su_root_run(root);
```

如果应用自己已经有循环，可以周期性调用 `su_root_step(root, timeout)`。
没有执行 `su_root_run()` 或 `su_root_step()`，NUA 的回调就不会被及时处理。

### 2.2 Agent：整个 SIP 栈

```c
nua_t *nua = nua_create(root,
                        app_callback,
                        app_context,
                        NUTAG_URL("sip:0.0.0.0:5060"),
                        TAG_END());
```

`nua_create()` 会创建 NUA agent，并建立由 `NUTAG_URL()`、
`NUTAG_SIPS_URL()` 等参数指定的传输。创建后可用：

```c
nua_set_params(nua, NUTAG_USER_AGENT("primer-ua"), TAG_END());
nua_get_params(nua, /* output tags */ TAG_END());
```

全局参数适合放在 agent 上；某个呼叫或订阅独有的参数应放在 handle 上。

### 2.3 Handle：一次操作的“档案夹”

```c
nua_handle_t *nh = nua_handle(nua,
                              operation_context,
                              NUTAG_URL("sip:1001@example.com"),
                              TAG_END());

nua_invite(nh, TAG_END());
```

Handle 保存对话、媒体、注册、订阅或简单事务的状态。入站 `INVITE`、
`MESSAGE` 等请求也会由 NUA 自动创建 handle，并把它传给 callback。

不要把 `nua_handle_t` 当成可直接访问的结构体；它是 opaque object，只能通过
NUA API 查询或修改。

### 2.4 Magic：应用自己的上下文

NUA 不理解应用上下文的内容，只保存指针并在 callback 时原样返回：

```c
typedef struct {
    su_root_t *root;
    nua_t *nua;
    /* 配置、日志、呼叫表等应用数据 */
} app_t;

typedef struct {
    nua_handle_t *handle;
    char call_id[128];
    /* 该呼叫自己的业务状态 */
} call_t;
```

推荐把 agent 级别的数据放入 `magic`，把单呼叫或单订阅数据放入
`hmagic`。这样 callback 收到 `nh` 时可以直接找到业务对象。

## 3. 异步线程模型

```text
应用线程                         NUA / stack 线程
   |                                   |
   | nua_invite(nh, ...)               |
   | ------------ 异步消息 ---------->|
   |                                   | 生成并发送 INVITE
   |                                   | 接收 180 / 200
   |<----------- callback -------------|
   | 处理 nua_r_invite / nua_i_state  |
```

官方模型允许应用和协议引擎运行在不同线程；NUA API 调用和 callback 之间通过
异步消息交互。callback 在 root 对应的执行上下文中被调用。[官方线程和消息模型](https://sofia-sip.sourceforge.net/refdocs/nua/index.html)

因此 callback 中应遵守三条规则：

1. 快速读取 `status`、`sip` 和 tags，并复制需要长期保存的数据。
2. 不要在 callback 中执行阻塞 I/O、长时间 SQL 或等待另一个线程。
3. `sip_t const *sip` 和 callback 中的 tag 数据只按回调期间有效处理；需要跨回调使用时复制内容。

FreeSWITCH 的 `mod_sofia` 也遵循这个边界：Sofia 线程负责 SIP I/O，业务处理
转交到 FreeSWITCH 自己的队列和线程。[Tech Stack — Sofia-SIP](../1.architecture/02-tech-stack.md#sofia-sip-library)

## 4. Tags：NUA 的参数系统

Sofia 使用 typed tags 模拟可变参数：

```c
nua_register(nh,
            SIPTAG_CONTACT_STR("<sip:1000@192.0.2.10>"),
            SIPTAG_EXPIRES_STR("3600"),
            TAG_END());
```

规则如下：

| 写法 | 用途 |
|---|---|
| `NUTAG_NAME(value)` | NUA 参数 |
| `SIPTAG_NAME(value)` | 传入已解析的 SIP 结构 |
| `SIPTAG_NAME_STR("...")` | 传入字符串，由 Sofia 解析 |
| `NUTAG_NAME_REF(x)` | 通过 `tl_gets()` 取回 NUA 参数 |
| `SIPTAG_NAME_REF(x)` | 通过 `tl_gets()` 取回解析后的头域 |
| `SIPTAG_NAME_STR_REF(x)` | 通过 `tl_gets()` 取回字符串形式的头域 |
| `TAG_END()` / `TAG_NULL()` | tag 列表结束，必须放在最后 |

相同头域可以重复传入，例如多个 `Accept`。对只能出现一次的头域，新的 tag
值会替换原值；`NULL` 不修改，特殊值 `(void *)-1` 可移除对应头域。

部分头域是 handle 的 sticky headers：`Contact`、`User-Agent`、`Supported`、
`Allow` 和 `Organization`。它们在 handle 级调用中设置后，可能自动出现在
后续请求中。[NUA API Overview](https://sofia-sip.sourceforge.net/refdocs/nua/nua_api_overview.html)

## 5. 最小生命周期

```c
su_init();
su_home_t home[1];
su_home_init(home);

app_t app = {0};
app.root = su_root_create(&app);
if (app.root) {
    app.nua = nua_create(app.root,
                         app_callback,
                         &app,
                         NUTAG_URL("sip:0.0.0.0:5060"),
                         TAG_END());

    if (app.nua) {
        nua_set_params(app.nua, TAG_END());
        su_root_run(app.root);
        nua_destroy(app.nua);
    }

    su_root_destroy(app.root);
}

su_home_deinit(home);
su_deinit();
```

生产代码应在退出时先调用 `nua_shutdown()`，等待 `nua_r_shutdown` 的最终
callback，再调用 `su_root_break()` 让事件循环退出，最后销毁 NUA 和 root。

## 6. 一句话记忆

```text
root 驱动事件循环，nua 管整个 SIP 栈，handle 管一次操作，magic 管业务上下文，tags 管参数，callback 管结果。
```

## 参考资料

- [Sofia-SIP NUA main page](https://sofia-sip.sourceforge.net/refdocs/nua/index.html)
- [NUA API Overview](https://sofia-sip.sourceforge.net/refdocs/nua/nua_api_overview.html)
- [FreeSWITCH Tech Stack — Sofia-SIP](../1.architecture/02-tech-stack.md#sofia-sip-library)

<!-- PKB-metadata
last_updated: 2026-09-17
commit: 4ff191a
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->
