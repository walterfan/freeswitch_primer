# 14. 在 FreeSWITCH 中打日志

<!-- maintained-by: human+ai -->

本文说明 C/C++ 模块如何使用 FreeSWITCH 的日志引擎。实现位于
`src/switch_log.c`，公开声明位于
`src/include/switch_log.h`。

## 最常用的写法

```c
#include <switch.h>

switch_log_printf(SWITCH_CHANNEL_LOG,
                  SWITCH_LOG_INFO,
                  "destination=%s retry=%d\n",
                  destination,
                  retry_count);
```

`SWITCH_CHANNEL_LOG` 会自动填充文件名、函数名和行号。格式字符串和参数
必须匹配；通常在消息末尾加 `\n`。

## 日志级别

`switch_log_level_t` 定义在 `src/include/switch_types.h`：

| 级别 | 用途 |
|---|---|
| `SWITCH_LOG_DEBUG` | 调试信息 |
| `SWITCH_LOG_INFO` | 正常运行信息 |
| `SWITCH_LOG_NOTICE` | 重要但不是异常的信息 |
| `SWITCH_LOG_WARNING` | 可恢复问题或配置风险 |
| `SWITCH_LOG_ERROR` | 操作失败，但进程可以继续 |
| `SWITCH_LOG_CRIT` | 严重错误 |
| `SWITCH_LOG_ALERT` | 需要立即处理的问题 |
| `SWITCH_LOG_CONSOLE` | 控制台级别消息 |

优先级数字越小越高。`switch_log_meta_vprintf()` 使用
`level > runtime.hard_log_level` 判断是否过滤日志。因此全局级别为 `INFO`
时，`DEBUG` 不会输出。

```c
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                  "codec state=%d\n", state);
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                  "Ignoring invalid timeout=%d\n", timeout);
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                  "Failed to open media resource\n");
```

## 与呼叫关联

有 `switch_core_session_t *session` 时，使用
`SWITCH_CHANNEL_SESSION_LOG(session)`：

```c
switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                  SWITCH_LOG_INFO,
                  "Call state changed to %s\n",
                  state);
```

日志节点会保存 Session UUID，后端可以据此关联同一个呼叫。只有 UUID 时使用
`SWITCH_CHANNEL_UUID_LOG(uuid)`。不需要自动前缀时使用
`SWITCH_CHANNEL_LOG_CLEAN`；需要产生 `SWITCH_EVENT_LOG` 时使用
`SWITCH_CHANNEL_EVENT`。

这些宏定义在 `src/include/switch_types.h`：

| 宏 | 用途 |
|---|---|
| `SWITCH_CHANNEL_LOG` | 普通日志，带时间、级别、文件和行号 |
| `SWITCH_CHANNEL_SESSION_LOG(session)` | 与 Session 关联 |
| `SWITCH_CHANNEL_UUID_LOG(uuid)` | 与 UUID 关联 |
| `SWITCH_CHANNEL_LOG_CLEAN` | 不自动增加日志前缀 |
| `SWITCH_CHANNEL_EVENT` | 发送 `SWITCH_EVENT_LOG` |

## 一个完整的模块示例

```c
#include <switch.h>

static switch_status_t start_media(switch_core_session_t *session,
                                    const char *codec_name)
{
    if (!session || zstr(codec_name)) {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR,
                          "Invalid media start parameters\n");
        return SWITCH_STATUS_FALSE;
    }

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                      SWITCH_LOG_INFO,
                      "Starting media with codec=%s\n",
                      codec_name);

    /* 媒体初始化代码 */

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                      SWITCH_LOG_NOTICE,
                      "Media started successfully\n");
    return SWITCH_STATUS_SUCCESS;
}
```

## DEBUG1 到 DEBUG10

`SWITCH_LOG_DEBUG1` 到 `SWITCH_LOG_DEBUG10` 用于更细的高频调试：

```c
switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                  SWITCH_LOG_DEBUG1,
                  "RTP packet count=%u\n",
                  packet_count);
```

实现会根据 `runtime.debug_level` 过滤。例如 `DEBUG5` 需要 debug level 至少
为 5，适合 RTP 和状态机细节，不应作为正常运行日志。

## 控制台、文件和 Syslog

`switch_log.c` 负责过滤、格式化和队列分发，具体后端由 logger 模块处理：

```text
switch_log_printf() -> LOG_QUEUE -> log_thread()
                    -> mod_console / mod_logfile / mod_syslog / mod_graylog2
```

常见配置文件：

```text
conf/vanilla/autoload_configs/console.conf.xml
conf/vanilla/autoload_configs/logfile.conf.xml
conf/vanilla/autoload_configs/syslog.conf.xml
```

控制台和文件 logger 可以按文件名、函数名或 `all` 过滤：

```xml
<mappings>
  <map name="all" value="warning,err,crit,alert"/>
  <map name="mod_sofia.c"
       value="debug,info,notice,warning,err,crit,alert"/>
</mappings>
```

运行时可调整控制台级别：

```text
fs_cli -x "console loglevel debug"
fs_cli -x "console loglevel info"
fs_cli -x "console loglevel warning"
```

## JSON metadata

需要结构化字段时使用 `switch_log_meta_printf()`：

```c
cJSON *meta = cJSON_CreateObject();
cJSON_AddStringToObject(meta, "operation", "transfer");
cJSON_AddNumberToObject(meta, "duration_ms", 42);

switch_log_meta_printf(SWITCH_CHANNEL_LOG,
                       SWITCH_LOG_INFO,
                       &meta,
                       "Operation completed\n");
```

`meta` 的所有权会转移给日志函数；调用后不要再次释放同一个对象。日志后端
可用 `switch_log_node_to_json()` 转换 `switch_log_node_t`。

## 自定义 logger

只有需要接入新的日志系统时才使用 `switch_log_bind_logger()`：

```c
static switch_status_t my_logger(const switch_log_node_t *node,
                                  switch_log_level_t level)
{
    if (!node || !node->content) {
        return SWITCH_STATUS_FALSE;
    }

    fprintf(stderr, "[%s] %s:%u %s",
            switch_log_level2str(level),
            node->file,
            node->line,
            node->content);
    return SWITCH_STATUS_SUCCESS;
}

switch_log_bind_logger(my_logger, SWITCH_LOG_DEBUG, SWITCH_FALSE);
/* 模块卸载时 */
switch_log_unbind_logger(my_logger);
```

logger 回调由 `log_thread()` 在绑定锁保护下调用，不应执行长时间阻塞的网络
操作，也不应形成递归日志链路；远程发送应交给模块自己的队列。

## 其他 API

| API | 用途 |
|---|---|
| `switch_log_printf()` / `switch_log_vprintf()` | 普通日志 |
| `switch_log_meta_printf()` / `switch_log_meta_vprintf()` | 带 JSON metadata |
| `switch_log_level2str()` | 级别数值转字符串 |
| `switch_log_str2level()` | 字符串转级别 |
| `switch_log_str2mask()` | 多个级别转 bit mask |
| `switch_log_check_mask()` | 检查 mask |
| `switch_log_node_dup()` / `switch_log_node_free()` | 复制和释放日志节点 |

普通业务代码不需要直接调用 `switch_log_node_*`、`switch_log_bind_logger()` 或
`switch_log_init()`。

## 排查日志不出现

1. 检查 `runtime.hard_log_level` 是否过滤了该级别。
2. 检查 `DEBUG1` 到 `DEBUG10` 是否超过当前 debug level。
3. 检查目标 logger 模块是否加载。
4. 检查 `console.conf.xml` 或 `logfile.conf.xml` 的 mapping。
5. 检查是否使用了 `SWITCH_CHANNEL_LOG_CLEAN`，导致没有文件行号前缀。

不要记录密码、Token、SIP digest、Voicemail PIN、TLS 私钥、完整客户内容或
不必要的个人信息。

## 代码定位

- API：`src/include/switch_log.h`
- 级别和 channel 宏：`src/include/switch_types.h`
- 核心实现：`src/switch_log.c`
- 控制台：`src/mod/loggers/mod_console/mod_console.c`
- 文件：`src/mod/loggers/mod_logfile/mod_logfile.c`
- Syslog：`src/mod/loggers/mod_syslog/mod_syslog.c`

<!-- PKB-metadata
last_updated: 2026-09-16
commit:
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->
