# mod_tutorial

本目录包含一个隔离的 FreeSWITCH 教学模块。它不会加入
`build/modules.conf.in` 或 `conf/vanilla`，只依赖已安装的
`freeswitch.pc`，因此可以在产品树之外单独构建。

## Build

```bash
cmake -S tutorial/module/mod_tutorial \
  -B tutorial/module/mod_tutorial/build \
  -DFREESWITCH_PC_FILE=/path/to/freeswitch/lib/pkgconfig/freeswitch.pc
cmake --build tutorial/module/mod_tutorial/build
cmake --install tutorial/module/mod_tutorial/build --prefix "$HOME/fs"
```

默认测试不需要运行中的 FreeSWITCH：

```bash
ctest --test-dir tutorial/module/mod_tutorial/build --output-on-failure
```

其中包括无依赖的计数/API 并发单元测试和 XML/生命周期 harness。要
执行真实加载、拨号、事件和可选 Prometheus 检查，必须明确开启集成
测试，并提供已安装配置和模块路径：

```bash
TUTORIAL_INTEGRATION=1 \
TUTORIAL_MODULE="$HOME/fs/lib/freeswitch/mod/mod_tutorial.so" \
TUTORIAL_MODULE_CONFIG="$HOME/fs/etc/freeswitch/autoload_configs/mod_tutorial.conf.xml" \
ctest --test-dir tutorial/module/mod_tutorial/build -R mod-tutorial-integration --output-on-failure
```

集成测试需要正在运行的隔离 FreeSWITCH、已挂载的
`tutorial-ivr.xml`，以及默认的 ESL `ClueCon`；非默认 ESL 环境请通过
`ESL_HOST`、`ESL_PORT` 和 `ESL_PASSWORD` 提供连接参数。不要在共享 shell
历史或公共日志中记录密码。

加载模块前，将 `mod_tutorial.conf.xml` 安装到
`autoload_configs/`；将 `tutorial-ivr.xml` 显式挂载到一个教程专用
dialplan include 目录（例如 `$${conf_dir}/tutorial/`），再在该环境
中 include 它。该文件使用 `tone_stream`，所以不依赖生产音频文件；
实际音频课程可以把同一位置的 prompt 替换成实验室音频包。

模块加载后，在运行中的 FreeSWITCH CLI 中执行：

```text
load mod_tutorial
tutorial_ivr_metric main 1
tutorial_metrics json
```

`tutorial_ivr_metric <menu> <choice>` 只接受配置中的菜单和
`0-9/*/#` 单字符选择。成功时设置 `tutorial_ivr_recorded=true`，
并发布 `CUSTOM tutorial::ivr_choice`；失败时只增加受限的无效计数，
不发布事件。`tutorial_metrics [text|json]` 返回稳定字段集合和配置
中每个菜单选择的计数。

拨打 `9000` 会进入示例 IVR：主菜单 `1` 进入二级菜单，主菜单
`2`/`3` 和二级菜单 `1`/`2` 都会调用教学应用并播放不同的提示音。
这些 XML 只属于教程，不会自动修改 vanilla 配置。

配置文件允许最多 8 个菜单和 32 个选择。菜单名限制为 32 个
字母数字、`_` 或 `-` 字符，选择必须是单个 `0-9`、`*` 或 `#`。

