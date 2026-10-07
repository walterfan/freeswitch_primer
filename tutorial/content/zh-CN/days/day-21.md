# 第 21 天：模块生命周期与独立构建

## 今日成果

- 用已安装的 `freeswitch.pc` 独立构建 `mod_tutorial.so`，不改 `build/modules.conf.in`。
- 跑通不依赖 SIP 的单元/契约测试。
- 理解 load：pool → mutex → 配置 → reserve event subclass → 注册 API/APP；shutdown 相反。

## 核心原理

教学模块是 standalone 共享库。`SWITCH_MODULE_DEFINITION` 导出 load/shutdown。失败时必须释放已创建的 mutex 和 event subclass，避免泄漏。配置来自 `mod_tutorial.conf.xml`，不写入 `conf/vanilla`。Compose overlay 把 `.so` 与 XML 挂进实验容器。

## 源码导航

- [`tutorial/module/mod_tutorial/CMakeLists.txt`](../../../module/mod_tutorial/CMakeLists.txt)
- [`tutorial/module/mod_tutorial/mod_tutorial.c`](../../../module/mod_tutorial/mod_tutorial.c)：`mod_tutorial_load` / `shutdown`
- [`build/standalone_module/`](../../../../build/standalone_module/)
- [`src/switch_loadable_module.c`](../../../../src/switch_loadable_module.c)
- [`tutorial/module/mod_tutorial/README.md`](../../../module/mod_tutorial/README.md)
- [本课实验目录](../../../labs/day-21/)

## 源码深挖

FreeSWITCH 加载共享库时，`switch_loadable_module_load_module_ex` 负责动态库和模块 hash；模块自己的 `SWITCH_MODULE_LOAD_FUNCTION` 才负责创建业务资源。教程模块的 load 顺序是：保存 pool/启动时间 → 创建 mutex → 解析 XML → reserve `tutorial::ivr_choice` → `SWITCH_ADD_API`/`SWITCH_ADD_APP`。任何一步失败都走 `cleanup_tutorial_resources`。

这个顺序很重要：先注册 API 再初始化配置，会让半加载模块对外可见；先 reserve event subclass 再处理失败路径，则必须在 cleanup 中释放。standalone CMake 只改变构建产物，不会自动改变 FreeSWITCH 的运行时 autoload 配置。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "switch_loadable_module_load_module_ex|SWITCH_MODULE_LOAD_FUNCTION|SWITCH_MODULE_SHUTDOWN_FUNCTION|SWITCH_ADD_API|SWITCH_ADD_APP" \
  "$FREESWITCH_SRC/src/switch_loadable_module.c" \
  tutorial/module/mod_tutorial/mod_tutorial.c
```

验收要同时证明 `.so` 能被加载、API/app 已注册、配置错误能回滚；只看到 CMake 生成 `.so` 不能证明模块可运行。

## 引导实验

前置条件：本机或容器能找到 `freeswitch.pc`。不要把模块编进 core `modules.conf`。

1. 构建与测试：

   ```bash
   cmake -S tutorial/module/mod_tutorial \
     -B tutorial/module/mod_tutorial/build \
     -DFREESWITCH_PC_FILE="$HOME/fs/lib/pkgconfig/freeswitch.pc"
   cmake --build tutorial/module/mod_tutorial/build
   ctest --test-dir tutorial/module/mod_tutorial/build --output-on-failure
   ls tutorial/module/mod_tutorial/build/mod_tutorial.so
   ```

   若本机没有 `$HOME/fs`，改成容器或 Ubuntu 主机上实际 pc 路径。`mod-tutorial-integration` 在未设 `TUTORIAL_INTEGRATION=1` 时 SKIP（代码 77），这是预期，不是 fail。

2. 对照 load 函数顺序阅读 `mod_tutorial_load`：mutex → `load_tutorial_config` → `switch_event_reserve_subclass` → `SWITCH_ADD_API` / `SWITCH_ADD_APP`。

3. 在隔离 FS 中显式加载（路径以挂载为准）：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'load mod_tutorial'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'module_exists mod_tutorial'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'unload mod_tutorial'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x status
   ```

   预期 load 后 `true`，unload 后 `false`，core 仍为 `UP`。有活动呼叫时不要 unload。

清理：unload。不要把 `.so` 提交进 git。

## 独立挑战

列出配置失败、mutex 失败、event subclass 失败时应释放什么，以及 memory pool 由谁拥有。

## 验收

**验收方式：自动加半自动。**

- **pass**：ctest 非集成项通过；有 `mod_tutorial.so`；能说明未改 modules.conf.in。
- **fail**：为了链接成功去改 core 构建清单。
- **unavailable**：没有 freeswitch.pc。

## 故障排查

- 现象：找不到 header → 证据：pkg-config 失败 → 原因：`FREESWITCH_PC_FILE` 错误 → 恢复：指向已安装 prefix。
- 现象：load 立即 TERM → 证据：日志 `unable to load mod_tutorial.conf` → 原因：未挂载 conf XML → 恢复：autoload_configs 挂载。
- 现象：unload 后接口仍在 → 证据：仍能调用 API → 原因：引用计数/通话占用 → 恢复：先 hangup。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 模块接口 | module interface | core 发现 API/APP/endpoint 的注册表项 |
| 内存池 | memory pool | 模块生命周期内的分配域 |
| 独立模块 | standalone module | 用已安装 SDK 构建、不改 core 清单 |
| 事件子类 | event subclass | `tutorial::ivr_choice` 的保留名 |
