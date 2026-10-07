# C++ 教学网站

本目录承载 C++17/Crow 教学服务与原生 HTML/CSS/JavaScript 前端。浏览器直接通过 SIP/WSS 与 FreeSWITCH 建立 WebRTC 音频；服务只负责课程、只读验收、ESL 事件、健康与 Metrics，不代理媒体。

## 构建与运行

默认配置只构建课程网站，不链接 ESL。启用观测器时，先在仓库中构建
`libs/esl`，再显式传入头文件目录和库文件；这样缺少 ESL 依赖会在 configure
阶段报错，而不会静默退化：

```bash
cmake -S tutorial/site -B tutorial/site/build \
  -DTUTORIAL_ENABLE_ESL=ON \
  -DTUTORIAL_ESL_INCLUDE="$PWD/libs/esl/src/include" \
  -DTUTORIAL_ESL_LIBRARY="$PWD/libs/esl/.libs/libesl.a"
cmake --build tutorial/site/build
```

```bash
./tutorial/site/build.sh
./tutorial/site/build/freeswitch-tutorial-site \
  --config tutorial/site/config/config.yaml
```

localhost HTTPS 和远端 FreeSWITCH WSS 实验使用：

```bash
./tutorial/deploy/generate-local-cert.sh
./tutorial/site/build/freeswitch-tutorial-site \
  --config tutorial/site/config/localhost-https.yaml
```

打开 `https://localhost:9443`。需要先信任 localhost 证书；远端 WSS 证书需要单独信任并匹配其地址，详见 [`deploy/README.md`](../deploy/README.md)。

## Container image

镜像构建使用 `conan.lock` 固定 C++ 依赖，SIP.js 和 markdown-it
继续使用仓库内的固定浏览器文件；镜像不包含教程证书或私钥：

```bash
docker build -f tutorial/site/Dockerfile \
  -t freeswitch-tutorial-site:local tutorial
docker run --rm --network host freeswitch-tutorial-site:local
```

默认配置只绑定 loopback，ESL 观测和实际 WSS 地址应通过教程部署
配置显式提供。镜像内的 `test/` 目录和 `THIRD_PARTY_NOTICES.md`
用于测试与许可证追踪，不代表已执行真实 SIP 或人工音频验收。
