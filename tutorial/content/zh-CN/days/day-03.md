# 第 3 天：核心与可加载模块

## 今日成果

- 能区分 FreeSWITCH core、共享模块文件、运行时模块接口和 XML 自动加载配置。
- 能分别证明一个模块“参与构建”“已安装”和“当前已加载”，不把三种状态混为一谈。
- 能解释为什么修改 build-time `modules.conf` 不会自动改变正在运行的 `modules.conf.xml`。

## 核心原理

FreeSWITCH core 提供 session、channel、事件、内存池、日志和模块装载框架；协议端点、dialplan application、codec、文件格式及事件处理器大多由 `mod_*` 动态模块提供。`mod_sofia` 是 SIP endpoint，`mod_commands` 提供许多 CLI/API 命令，`mod_event_socket` 暴露 ESL。模块卸载后 core 可以继续运行，但依赖该接口的新操作会不可用。

模块有三种容易混淆的状态：

1. **参与构建**：源码构建树的 `modules.conf` 含有 `src/mod/...` 条目。仓库提供的 [`build/modules.conf.in`](../../../../build/modules.conf.in) 是生成构建清单的输入，不是运行时加载列表。
2. **已安装**：安装前缀的 `mod/` 目录存在对应 `.so`。文件存在只说明可供装载，不证明已经加载。
3. **当前已加载**：FreeSWITCH module registry 中存在模块及其导出的 API、application、endpoint 等接口。可用 `module_exists` 和 `show modules` 取得运行时证据。

运行时 [`modules.conf.xml`](../../../../conf/vanilla/autoload_configs/modules.conf.xml) 的 `<load module="mod_…"/>` 决定启动时尝试装载哪些已安装模块。它不能让一个未编译、未安装的模块凭空出现。反过来，已安装模块也可以不自动加载。后续第 21–25 天的 `mod_tutorial` 会采用独立构建和教程专用挂载，不修改 `build/modules.conf.in`，也不向 `conf/vanilla` 添加文件。

## 源码导航

- [`src/switch.c`](../../../../src/switch.c)：进程入口调用 core 初始化与模块装载流程。
- [`src/switch_loadable_module.c`](../../../../src/switch_loadable_module.c)：模块注册表、装载、卸载及接口查找的核心实现。
- [`src/include/switch_loadable_module.h`](../../../../src/include/switch_loadable_module.h)：模块宏、生命周期函数和接口类型声明。
- [`src/mod/endpoints/mod_sofia/mod_sofia.c`](../../../../src/mod/endpoints/mod_sofia/mod_sofia.c)：实际 endpoint 模块，使用 `SWITCH_MODULE_LOAD_FUNCTION` 等模块契约。
- [`build/modules.conf.in`](../../../../build/modules.conf.in)：build-time 默认编译输入。
- [`conf/vanilla/autoload_configs/modules.conf.xml`](../../../../conf/vanilla/autoload_configs/modules.conf.xml)：演示配置中的 runtime 自动加载清单。
- [`build/standalone_module/`](../../../../build/standalone_module/)：不改 core 构建清单的独立模块模板，第 21 天会复用该契约。

## 源码深挖

编译期的 `modules.conf` 只是决定哪些模块被编译；运行期的 `autoload_configs/modules.conf.xml` 才决定哪些 `.so` 被加载。加载器 `switch_loadable_module_load_module_ex` 会拼出模块路径、检查 module hash，打开共享库并查找模块定义；模块的 load 回调再注册 endpoint、application、API 或 dialplan 接口。

以 `mod_sofia` 为例，模块加载成功不代表 profile 已经启动：`mod_sofia.c` 的 load 回调先注册 Sofia 接口，profile 配置还要继续被解析并启动 listener。也因此 `module_exists mod_sofia=true` 与 `sofia status` 没有 profile 是两种不同故障。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "switch_loadable_module_load_module_ex|SWITCH_MODULE_DEFINITION|SWITCH_ADD_ENDPOINT|SWITCH_ADD_APP|SWITCH_ADD_API" \
  "$FREESWITCH_SRC/src/switch_loadable_module.c" \
  "$FREESWITCH_SRC/src/mod/endpoints/mod_sofia/mod_sofia.c"
```

实际排障按这个顺序做：文件存在 → module hash 中存在 → 接口已注册 → profile/listener RUNNING。不要只重启容器来掩盖“编译了但没 autoload”或“模块加载了但配置失败”。

## 引导实验

前置条件：远端 Ubuntu/Debian 实验主机上的 `freeswitch` 容器正在运行；本课只读，不执行 `load`、`unload` 或修改 XML。

1. 在源码树确认两个 build-time 条目：

   ```bash
   grep -n 'mod_sofia\|mod_event_socket' build/modules.conf.in
   ```

   这里的值采用 `applications/...`、`endpoints/...` 等源码相对路径；它不是 `<load module="..."/>` XML。

2. 在容器内清点已安装共享模块：

   ```bash
   docker exec freeswitch sh -lc \
     'find /usr/local/freeswitch/mod -maxdepth 1 -type f -name "*.so" | sort | head'
   docker exec freeswitch test -f \
     /usr/local/freeswitch/mod/mod_sofia.so
   ```

   第二条命令返回 0 证明 `mod_sofia.so` 已安装，但尚未证明 profile 或模块 registry 状态。

3. 检查 runtime 自动加载意图：

   ```bash
   docker exec freeswitch grep -n 'mod_sofia' \
     /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml
   ```

   预期看到 `<load module="mod_sofia"/>`。不要编辑运行中的 `freeswitch.xml.fsxml`；它是 `log_dir` 下内存映射的预处理结果。

4. 取得当前加载证据：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'module_exists mod_sofia'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'module_exists mod_tutorial'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'show modules' | head
   ```

   当前实验环境中，`mod_sofia` 返回 `true`，尚未开发和安装的 `mod_tutorial` 返回 `false`。`show modules` 输出的是导出接口清单，所以同一个文件可能出现多行；不要把行数误当模块文件数。

5. 把模块证据与 endpoint 证据关联：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'sofia status profile internal'
   ```

   `module_exists mod_sofia=true` 说明模块已加载；internal profile 为 `RUNNING` 才说明这份 SIP profile 已成功启动。两个结论层级不同。

清理：本课没有产生运行时变更。不要为了练习而卸载 `mod_sofia` 或 `mod_event_socket`，它们分别会中断 SIP 和本教程使用的 ESL 管理路径。

## 独立挑战

任选一个当前已安装模块，收集“共享文件存在”“XML 是否配置自动加载”“module registry 是否存在”三条证据。构造一个三列表格并解释任意两列不一致时的合理原因。不得通过加载或卸载生产/共享实验模块来制造差异。

## 验收

**验收方式：自动**

- **pass**：`module_exists mod_sofia` 返回 `true`，能从 `show modules` 找到它导出的接口，并正确指出 build-time `modules.conf`、runtime `modules.conf.xml` 和安装目录各自的职责。
- **fail**：命令可执行，但 `mod_sofia.so` 已安装且 runtime XML 声明加载后，module registry 仍明确不存在；保存脱敏启动日志，检查 XML 与动态链接错误。
- **unavailable**：容器、`fs_cli` 或安装前缀不可达，无法取得当前状态；不能用源码清单推断运行时成功。

已在 Ubuntu 22.04.5 LTS 的 `10.100.212.8` 实验主机验证：容器内有 48 个 `.so`，runtime XML 有 91 个 `<load module>` 条目，`mod_sofia=true`、`mod_tutorial=false`，internal profile 为 RUNNING。数量只记录本次环境，不作为其他构建的固定断言。

## 故障排查

- 现象：源码清单有模块但安装目录没有 `.so` → 证据：构建配置与安装树不一致 → 原因：未重新 configure/build/install、依赖缺失或使用了另一安装前缀 → 恢复：确认实际 build 目录和 prefix 后重新构建，不改 runtime XML 掩盖问题。
- 现象：`.so` 存在但 `module_exists` 为 `false` → 证据：安装成功、装载失败或未请求装载 → 原因：`modules.conf.xml` 未声明、依赖库缺失或模块 load 函数失败 → 恢复：检查有限启动日志和 `ldd`，在隔离实验中修复后再显式加载。
- 现象：模块存在但 Sofia profile 不见 → 证据：`module_exists mod_sofia=true`、profile 非 RUNNING → 原因：profile XML、绑定端口或证书失败 → 恢复：检查 Sofia profile 日志和配置，不重编译无关 core。
- 现象：误把 `modules.conf.xml` 当编译清单 → 证据：添加 `<load>` 后仍提示找不到模块文件 → 原因：模块未编译安装 → 恢复：回到 build-time 清单和依赖检查，保持两类配置名称与作用分离。

分享 `show modules` 或日志前删除地址、号码和完整 UUID；若临时提高日志级别，收集完证据后恢复。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 核心 | core | 提供 session、事件、内存和模块装载框架的基础运行时 |
| 可加载模块 | loadable module | 运行时注册 API、application、endpoint 等接口的共享库 |
| 构建清单 | build manifest | configure/build 阶段决定编译哪些源码模块的列表 |
| 自动加载清单 | autoload manifest | 运行时启动阶段请求加载哪些已安装模块的 XML |
| 模块注册表 | module registry | core 当前已加载模块及导出接口的内存状态 |
| 独立模块 | standalone module | 通过已安装 FreeSWITCH SDK 构建、不修改 core 编译清单的扩展 |
