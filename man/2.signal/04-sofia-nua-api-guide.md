# 19. Sofia-SIP NUA：API、事件和 Tags 速查

<!-- maintained-by: human+ai -->

本章把 NUA API 按“应用要做什么”分类。完整函数、参数和枚举仍应以当前
Sofia-SIP 头文件为准；这里的目标是让读代码和写最小调用路径变得容易。

## 1. Agent 和事件循环

| 任务 | API/Tag | 说明 |
|---|---|---|
| 初始化 Sofia | `su_init()` | 进程级初始化 |
| 创建事件循环 | `su_root_create()` | 返回 `su_root_t *` |
| 创建 NUA | `nua_create()` | 注册 callback，并创建传输 |
| 设置全局参数 | `nua_set_params()` | agent 级配置 |
| 查询全局参数 | `nua_get_params()` | 用 `_REF` tags 接收结果 |
| 运行事件循环 | `su_root_run()` / `su_root_step()` | 驱动 callback |
| 优雅关闭 | `nua_shutdown()` | 等待 `nua_r_shutdown` |
| 退出事件循环 | `su_root_break()` | 通常在 shutdown callback 中调用 |
| 销毁栈 | `nua_destroy()` | 停止后的最终清理 |

最小启动骨架：

```c
app.root = su_root_create(&app);
app.nua = nua_create(app.root,
                     app_callback,
                     &app,
                     NUTAG_URL("sip:0.0.0.0:5060"),
                     NUTAG_USER_AGENT("primer-ua"),
                     TAG_END());

if (app.nua) {
    su_root_run(app.root);
    nua_destroy(app.nua);
}
su_root_destroy(app.root);
```

传输相关的常见参数包括 `NUTAG_URL()`、`NUTAG_SIPS_URL()`、
`NUTAG_CERTIFICATE_DIR()` 和 `NUTAG_SIP_PARSER()`。

## 2. Handle 管理

```c
nua_handle_t *nh = nua_handle(nua,
                              handle_context,
                              NUTAG_URL("sip:1001@example.com"),
                              SIPTAG_TO_STR("<sip:1001@example.com>"),
                              TAG_END());

nua_get_hparams(nh, /* output tags */ TAG_END());
nua_set_hparams(nh, SIPTAG_SUBJECT_STR("support"), TAG_END());

if (nua_handle_has_active_call(nh)) {
    /* handle 上有活动呼叫 */
}

nua_handle_destroy(nh);
```

常用 handle 查询：

```text
nua_handle_has_invite()
nua_handle_has_subscribe()
nua_handle_has_register()
nua_handle_has_active_call()
nua_handle_has_call_on_hold()
nua_handle_has_events()
nua_handle_has_registrations()
nua_handle_remote()
nua_handle_local()
```

一个 handle 可以承载对话相关状态；业务代码仍应自己保存呼叫 ID、用户 ID、
媒体资源和清理状态。

## 3. 出站请求 API

| SIP 目的 | NUA 函数 | 典型响应事件 |
|---|---|---|
| 注册 | `nua_register()` | `nua_r_register` |
| 注销 | `nua_unregister()` | `nua_r_unregister` |
| 呼叫 | `nua_invite()` | `nua_r_invite`、`nua_i_state` |
| 取消呼叫 | `nua_cancel()` | `nua_r_cancel` |
| ACK | `nua_ack()` | 通常没有独立业务结果 |
| 挂断 | `nua_bye()` | `nua_r_bye` |
| 能力探测 | `nua_options()` | `nua_r_options` |
| 转接 | `nua_refer()` | `nua_r_refer`、`nua_i_notify` |
| 发布事件 | `nua_publish()` | `nua_r_publish` |
| 撤销发布 | `nua_unpublish()` | `nua_r_unpublish` |
| PRACK | `nua_prack()` | `nua_r_prack` |
| 对话内 INFO | `nua_info()` | `nua_r_info` |
| 更新会话 | `nua_update()` | `nua_r_update` |
| 即时消息 | `nua_message()` | `nua_r_message` |
| 订阅 | `nua_subscribe()` | `nua_r_subscribe`、`nua_i_notify` |
| 取消订阅 | `nua_unsubscribe()` | `nua_r_unsubscribe` |
| 发送通知 | `nua_notify()` | `nua_r_notify` |
| 自定义方法 | `nua_method()` | `nua_r_method` |

这些函数都使用 tag 列表传参，最后必须使用 `TAG_END()` 或 `TAG_NULL()`。
[NUA API Overview 的 API 分组](https://sofia-sip.sourceforge.net/refdocs/nua/nua_api_overview.html)

## 4. 入站请求 API

入站请求通过 callback 事件交给应用，应用用 `nua_respond()` 回复：

| 入站事件 | 请求 | 应用动作 |
|---|---|---|
| `nua_i_invite` | INVITE | `nua_respond()` 振铃、接听或拒绝 |
| `nua_i_cancel` | CANCEL | 通常由 NUA/事务层处理，应用观察状态 |
| `nua_i_ack` | ACK | 确认 2xx，通常进入 READY |
| `nua_i_bye` | BYE | 清理媒体和业务资源 |
| `nua_i_options` | OPTIONS | 返回能力信息 |
| `nua_i_message` | MESSAGE | 读取 payload，返回 2xx 或错误 |
| `nua_i_info` | INFO | 按 Content-Type 处理 body |
| `nua_i_update` | UPDATE | 处理早期/已建立会话变更 |
| `nua_i_refer` | REFER | 执行转接策略并发送 NOTIFY |
| `nua_i_subscribe` | SUBSCRIBE | 授权或拒绝订阅 |
| `nua_i_notify` | NOTIFY | 更新订阅状态和事件内容 |
| `nua_i_publish` | PUBLISH | 接收或合并发布状态 |
| `nua_i_register` | REGISTER | 如实现 Registrar，处理注册 |
| `nua_i_method` | 自定义方法 | 结合 `NUTAG_APPL_METHOD()` 处理 |

## 5. 典型参数写法

### 5.1 字符串头域

```c
nua_message(nh,
            SIPTAG_CONTENT_TYPE_STR("text/plain"),
            SIPTAG_PAYLOAD_STR("Hello, world!"),
            TAG_END());
```

### 5.2 已解析结构

```c
sip_to_t *to = sip_to_make(home, "<sip:1001@example.com>");
nua_invite(nh, SIPTAG_TO(to), TAG_END());
```

### 5.3 读取 callback tags

```c
int callstate = nua_callstate_init;
sip_event_t const *event = NULL;

tl_gets(tags,
        NUTAG_CALLSTATE_REF(callstate),
        SIPTAG_EVENT_REF(event),
        TAG_END());
```

实际类型以所使用版本的头文件为准；`_REF` 的关键语义是“取引用”，不是
复制所有权。需要跨 callback 保存时，应复制成应用自己的数据。

## 6. 高频功能的最小调用路径

### 6.1 注册

```c
nua_register(nh,
             NUTAG_REGISTRAR("sip:example.com"),
             SIPTAG_CONTACT_STR("<sip:1000@192.0.2.10>"),
             SIPTAG_EXPIRES_STR("3600"),
             TAG_END());
```

收到 `401/407` 时，使用 `nua_authenticate()`、`NUTAG_AUTH()` 或应用已有的
认证配置重新提交请求；不要手工拼接 Digest 响应，除非确实需要自定义认证。

### 6.2 MESSAGE

```c
nua_message(nh,
            SIPTAG_CONTENT_TYPE_STR("text/plain"),
            SIPTAG_PAYLOAD_STR("hello"),
            TAG_END());
```

在 `nua_i_message` 中读取 `sip->sip_payload`，按 `Content-Type` 决定如何解析；
NUA 只负责 SIP 操作，不替应用解释业务 body。

### 6.3 SUBSCRIBE

```c
nua_subscribe(nh,
              SIPTAG_EVENT_STR("presence"),
              SIPTAG_ACCEPT_STR("application/pidf+xml"),
              SIPTAG_EXPIRES_STR("3600"),
              TAG_END());
```

`nua_i_notify` 中读取 `Event`、`Subscription-State` 和 payload。NUA 可以管理
订阅刷新，但事件包语义和 XML/JSON body 由应用处理。

### 6.4 PUBLISH

```c
nua_publish(nh,
            SIPTAG_EVENT_STR("presence"),
            SIPTAG_CONTENT_TYPE_STR("application/pidf+xml"),
            SIPTAG_PAYLOAD_STR(presence_xml),
            TAG_END());
```

应用应保存成功响应中的 `SIP-ETag`，后续更新使用 `SIP-If-Match`。撤销时调用
`nua_unpublish()`。

### 6.5 REFER / 转接

```c
nua_refer(nh,
          SIPTAG_REFER_TO_STR("<sip:1002@example.com>"),
          TAG_END());
```

转接进度通常通过 `NOTIFY` 汇报。需要关联被转接对话时，可结合
`Replaces`、`Referred-By` 和 `nua_handle_make_replaces()`。

## 7. 呼叫相关 Tags

| 目的 | 常见 Tags |
|---|---|
| 传输地址 | `NUTAG_URL()`、`NUTAG_SIPS_URL()` |
| 代理/路由 | `NUTAG_PROXY()`、`NUTAG_INITIAL_ROUTE()` |
| 能力 | `NUTAG_ALLOW()`、`NUTAG_SUPPORTED()` |
| 自动呼叫行为 | `NUTAG_AUTOACK()`、`NUTAG_AUTOALERT()`、`NUTAG_AUTOANSWER()` |
| 呼叫状态 | `NUTAG_CALLSTATE()`、`NUTAG_CALLSTATE_REF()` |
| 呼叫定时器 | `NUTAG_INVITE_TIMER()`、`NUTAG_SESSION_TIMER()` |
| 媒体开关 | `NUTAG_MEDIA_ENABLE()` |
| SDP | `SOATAG_USER_SDP()`、`SOATAG_USER_SDP_STR()`、`SOATAG_CAPS_SDP()` |
| Offer/Answer | `NUTAG_EARLY_ANSWER()`、`NUTAG_EARLY_MEDIA()` |
| Session Timer | `NUTAG_MIN_SE()`、`NUTAG_SESSION_REFRESHER()`、`NUTAG_UPDATE_REFRESH()` |

## 8. 认证、事件服务器和自定义方法

### 8.1 认证

```text
收到 401/407
   -> nua_r_* callback
   -> nua_authenticate() / NUTAG_AUTH()
   -> NUA 重发原操作
```

认证缓存可通过 `NUTAG_AUTH_CACHE()` 控制。应用需要记录最终状态，避免错误
密码导致无限重试。

### 8.2 内置事件服务器

NUA 还提供 notifier、subscription、authorize 和 terminate 相关 API，可用于
构建简单的 Presence 或其他 SIP event server：

```text
nua_notifier()
nua_i_subscription
nua_authorize()
nua_notify()
nua_terminate()
```

应用负责决定是否授权订阅、事件 body 内容和事件包语义。

### 8.3 自定义 SIP 方法

```c
nua_method(nh,
           NUTAG_METHOD("X-APP-EVENT"),
           NUTAG_DIALOG(1),
           SIPTAG_CONTENT_TYPE_STR("text/plain"),
           SIPTAG_PAYLOAD_STR("data"),
           TAG_END());
```

自定义方法还需要通过 `NUTAG_ALLOW()` 或 `NUTAG_APPL_METHOD()` 声明，并在
`nua_i_method` / `nua_r_method` 中处理结果。

## 9. 实现检查清单

```text
[ ] root 已创建且 su_root_run/step 正在运行
[ ] nua_create() 的 callback 和 magic 生命周期有效
[ ] 每个出站操作有自己的 handle/context
[ ] 每个 tag 列表以 TAG_END() 结束
[ ] callback 内没有阻塞操作
[ ] SIP/SDP/payload 需要长期使用时已经复制
[ ] 401/407 有明确的重试上限
[ ] INVITE 成功后的 ACK 由 auto-ack 或应用明确负责
[ ] BYE/CANCEL 的竞态已覆盖
[ ] nua_r_bye 或 nua_i_bye 后再清理呼叫资源
[ ] nua_shutdown 完成后再销毁 root 和 nua
```

## 参考资料

- [NUA API Overview](https://sofia-sip.sourceforge.net/refdocs/nua/nua_api_overview.html)
- [Sofia-SIP NUA main page](https://sofia-sip.sourceforge.net/refdocs/nua/index.html)
- [NUA Call Model](https://sofia-sip.sourceforge.net/refdocs/nua/nua_call_model.html)
- [Sofia-SIP `nua` page index](https://sofia-sip.sourceforge.net/refdocs/nua/pages.html)

<!-- PKB-metadata
last_updated: 2026-09-17
commit: 4ff191a
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->
