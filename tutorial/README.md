# FreeSWITCH 30 天开发者教程

这是一套中文优先、项目驱动的 FreeSWITCH 学习路径。目标是在 30 天内完成从 SIP 基础、WebRTC 音频、IVR 和 ESL，到 C/C++ 模块二次开发与 Metrics 的完整闭环。

> **仅用于隔离实验环境。** 本教程会使用演示账号、SIP/WSS、RTP、ESL 和本地 HTTP 端口。不要把 vanilla 密码、`ClueCon`、私钥或教程配置直接部署到生产环境。远程使用麦克风时必须采用可信 HTTPS/WSS，分享日志和抓包前必须脱敏。

## 学习入口

- [30 天课程表](content/zh-CN/syllabus.md)
- [课程 manifest](content/manifest.json)
- [每日课程模板](content/zh-CN/days/_template.md)
- [实验素材](labs/README.md)
- [C++ 教学网站](site/README.md)
- [`mod_tutorial` 教学模块](module/mod_tutorial/README.md)
- [Docker、证书与 Prometheus](deploy/README.md)
- [测试与内容校验](tests/README.md)

## 目录职责

```text
tutorial/
├── content/   # 课程正文、manifest 与本地化结构
├── site/      # C++/Crow 服务和原生 HTML/CSS/JavaScript
├── module/    # 独立构建的 FreeSWITCH 教学模块
├── labs/      # 30 天共享实验素材，不复制到各语言目录
├── deploy/    # 教程专用部署、证书和可选监控配置
├── scripts/   # 内容校验等开发工具
└── tests/     # 单元、集成、smoke 与人工验收入口
```

教程不会修改 FreeSWITCH core，也不会向 `conf/vanilla` 添加模块配置。课程引用仓库内的 [`man/`](../man/index.md) 作为架构、源码导航和运行手册入口。

## 当前实施状态

30 个中文 lesson 已按 manifest 中的稳定 lesson ID 提供。网站、独立
`mod_tutorial`、教程 IVR、ESL/Metrics 观测和部署素材已经具备；英语
内容仍是后续工作，代码与实验素材保持共享。

第 5–30 天的正文按第 1–4 天同一标准写了可执行实验：命令、预期证据、分层排障和清理。共享口令：[`labs/COMMON.md`](labs/COMMON.md)。

推荐从这里开始：

1. 先阅读 [第 1 天](content/zh-CN/days/day-01.md) 并运行内容校验。
2. 按 [`site/README.md`](site/README.md) 准备 C++ 教学服务；仅在隔离
   环境中再启用 ESL、浏览器麦克风和 WSS。
3. 按 [`module/mod_tutorial/README.md`](module/mod_tutorial/README.md)
   独立构建教学模块，再按 [`deploy/README.md`](deploy/README.md) 挂载
   教程 XML。

已验证的本地环境包括 macOS 上的 Clang/CMake、Python 3 和已安装的
FreeSWITCH `freeswitch.pc`；目标运行基线仍是文档中的 Ubuntu/Debian
Docker lab。浏览器双向音频、真实 ESL、声音包和 Docker 启动需要在
对应隔离环境中复验，默认自动测试不会伪造这些结果。

## 基础校验

```bash
cmake -S tutorial -B tutorial/build
cmake --build tutorial/build
ctest --test-dir tutorial/build --output-on-failure
```

也可以使用 `tutorial/Makefile` 构建并管理本地教学网站：

```bash
make -C tutorial build
make -C tutorial start
make -C tutorial stop
```

网站默认监听 `127.0.0.1:7009`；进程 PID 和日志分别保存在被忽略的
`tutorial/site/.tutorial-site.pid` 与 `tutorial/site/.tutorial-site.log`。

独立模块测试（不需要生产 SIP trunk）：

```bash
cmake -S tutorial/module/mod_tutorial \
  -B tutorial/module/mod_tutorial/build \
  -DFREESWITCH_PC_FILE="$HOME/fs/lib/pkgconfig/freeswitch.pc"
cmake --build tutorial/module/mod_tutorial/build
ctest --test-dir tutorial/module/mod_tutorial/build --output-on-failure
```

完整课程正文就绪后，运行：

```bash
python3 tutorial/scripts/validate_content.py tutorial/content/manifest.json
```
