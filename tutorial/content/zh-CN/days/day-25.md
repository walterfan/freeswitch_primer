# 第 25 天：Load/unload、失败路径、日志与源码调试

## 今日成果

- 演练配置缺失、错误 XML、正常 load/unload 三条路径。
- 用 NOTICE/ERROR 日志定位，而不是 core dump。
- 明确活动呼叫时 unload 的风险。

## 核心原理

load 失败返回 `SWITCH_STATUS_TERM` 并 `cleanup_tutorial_resources`。shutdown 释放 subclass 与 mutex。日志用 `switch_log_printf`；教学模块不打印密码或完整用户 XML。

调试优先：`console loglevel debug` 数秒 → 复现 → `notice`。gdb 仅在隔离实验且无生产呼叫时使用。

## 源码导航

- [`tutorial/module/mod_tutorial/mod_tutorial.c`](../../../module/mod_tutorial/mod_tutorial.c)：cleanup / shutdown
- [`src/switch_loadable_module.c`](../../../../src/switch_loadable_module.c)
- [`tutorial/module/mod_tutorial/test/test_mod_tutorial.py`](../../../module/mod_tutorial/test/test_mod_tutorial.py)
- [本课实验目录](../../../labs/day-25/)

## 源码深挖

动态 unload 的根路径是模块 shutdown 回调，而不是简单删除 `.so` 文件。教程 `mod_tutorial_shutdown` 调用同一个 cleanup 函数，先释放已 reserve 的 event subclass，再销毁 mutex；load 失败也复用它，保证“失败发生在资源创建前/后”都能安全返回 `SWITCH_STATUS_TERM`。

日志应说明哪一阶段失败：XML 文件打不开、菜单边界非法、mutex 初始化失败、event subclass 冲突，还是 API/app 注册失败。调试时优先使用模块日志和 `module_exists`，不要在活动呼叫上随意 unload；已有 channel 可能仍保存 application/interface 指针。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "cleanup_tutorial_resources|SWITCH_STATUS_TERM|mod_tutorial_load|mod_tutorial_shutdown|switch_event_free_subclass|switch_mutex_destroy" \
  tutorial/module/mod_tutorial/mod_tutorial.c \
  "$FREESWITCH_SRC/src/switch_loadable_module.c"
```

失败演练必须在隔离 FS 和无活动通话环境中做；生产环境只验证可观测性，不用 unload 当作常规修复。

## 引导实验

前置条件：能改容器内教程 conf 的**副本**（bind mount 只读时在临时目录测试，或使用独立 FS）。

1. 正常路径：`load` → `module_exists true` → `tutorial_metrics` → `unload` → `false` → `status` 仍 UP。

2. 失败路径：把 conf 里 `<menus>` 删空（临时文件），load 应失败并在 `docker logs --tail 50 freeswitch` 看到 `at least one menu choice is required` 或同类 ERROR。恢复原 XML 后再 load。

3. 契约测试：

   ```bash
   ctest --test-dir tutorial/module/mod_tutorial/build -R mod-tutorial-contract --output-on-failure
   ```

4. 有一通 park 的 loopback 时尝试 unload，记录是否拒绝或挂断。笔记写清风险：教学环境可实验，生产模块不要对直播呼叫 unload。

清理：恢复 XML；loglevel notice；unload 多余模块。

## 独立挑战

写一份“模块无法加载”检查单：so 是否存在、conf 是否被 xml 打开、event subclass 是否冲突、依赖库 ldd。

## 验收

**验收方式：半自动。**

- **pass**：成功与失败日志都能指出；core 未崩溃。
- **fail**：失败后编辑 `freeswitch.xml.fsxml`。
- **unavailable**：conf 只读无法演练失败路径，则只跑 contract 测试。

## 故障排查

- 现象：unload 崩溃 → 证据：double free subclass → 原因：cleanup 调用两次 → 恢复：看 `event_reserved` 标志。
- 现象：load 成功但 API 未知 → 证据：show modules 无 tutorial_metrics → 原因：加载了别的 so → 恢复：核对挂载路径。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 终止状态 | SWITCH_STATUS_TERM | load 失败，模块不进入运行表 |
| 关闭 | shutdown | 卸载时释放资源 |
| 契约测试 | contract test | 不启动 SIP 的 XML/生命周期检查 |
