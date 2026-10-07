# 教程部署

本目录保存教程专用 Docker Compose overlay、可信本地证书说明及可选 Prometheus 配置。默认服务只发布到本机；这里的文件不会替换仓库现有 Docker 环境。

## localhost HTTPS

先生成一个仅用于实验的本地 CA，以及由它签发、只包含 `localhost`、
`127.0.0.1` 和 `::1` SAN 的 30 天网站证书：

```bash
./tutorial/deploy/generate-local-cert.sh
```

所有私钥写入被 Git 忽略的 `tutorial/deploy/certs/`。只将
`tutorial-lab-ca.crt` 导入当前操作系统或实验浏览器的信任库；不要导入、复制
或提交 `tutorial-lab-ca.key`。macOS 可在“钥匙串访问”中把该 CA 设置为始终信任，
然后完全退出并重新打开浏览器。再启动网站：

```bash
./tutorial/site/build-ro/freeswitch-tutorial-site \
  --config tutorial/site/config/localhost-https.yaml
curl --cacert tutorial/deploy/certs/tutorial-lab-ca.crt \
  https://localhost:9443/api/v1/health
```

证书被“信任”和证书“名称匹配”是两个独立条件。这个 localhost 证书只保护教学网站，不能用于 `10.100.212.8:7443`。FreeSWITCH WSS 必须使用包含实际域名或 IP SAN、且被浏览器信任的另一张证书；不要通过关闭证书校验来绕过。

## 远程 FreeSWITCH WSS

当前实验服务器使用 `10.100.212.8`，生成由同一个实验 CA 签发、SAN 精确匹配该
IP 的组合 PEM：

```bash
./tutorial/deploy/generate-freeswitch-wss-cert.sh
openssl x509 -in tutorial/deploy/certs/freeswitch-wss.crt \
  -noout -subject -issuer -ext subjectAltName
```

远端容器当前从 `/usr/local/freeswitch/certs/wss.pem` 读取 WSS 证书。下面的命令
从本机执行；安装前先在远端 home 目录保存原文件，安装后只重启 internal Sofia
profile，并用 CA 和 IP 名称共同验证：

```bash
scp tutorial/deploy/certs/wss.pem \
  walter@10.100.212.8:/home/walter/freeswitch-tutorial-wss.pem
ssh walter@10.100.212.8 \
  'docker cp freeswitch:/usr/local/freeswitch/certs/wss.pem \
     /home/walter/wss.pem.before-tutorial && \
   docker cp /home/walter/freeswitch-tutorial-wss.pem \
     freeswitch:/usr/local/freeswitch/certs/wss.pem && \
   docker exec freeswitch chmod 600 /usr/local/freeswitch/certs/wss.pem && \
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x "sofia profile internal restart reloadxml"'
openssl s_client -connect 10.100.212.8:7443 -servername 10.100.212.8 \
  -CAfile tutorial/deploy/certs/tutorial-lab-ca.crt \
  -verify_ip 10.100.212.8 -verify_return_error </dev/null
```

`wss.pem.before-tutorial` 是回滚文件；实验结束时复制回容器并重启同一 profile。
容器重建会丢失容器内的手工替换，因此后续 Compose overlay 会显式挂载教程证书。

当前实验拓扑中，网站运行在本机 `https://localhost:9443`，浏览器直接连接远端 `wss://10.100.212.8:7443`。ESL 继续由远端 ACL 限制，不能因为网站在本机就把 8021 暴露到非可信网络。

## 本地 Compose overlay

教程 overlay 会 include 已记录的 Debian 11 FreeSWITCH Compose，
并显式挂载 `mod_tutorial.conf.xml`、`tutorial-ivr.xml` 和构建好的模块；
它不会修改基线 Compose 或产品 vanilla 配置。先独立构建模块并把安装产物
路径传给 Compose：

```bash
cmake -S tutorial/module/mod_tutorial -B tutorial/module/mod_tutorial/build \
  -DFREESWITCH_PC_FILE="$HOME/fs/lib/pkgconfig/freeswitch.pc"
cmake --build tutorial/module/mod_tutorial/build
TUTORIAL_ESL_PASSWORD='仅限隔离实验' \
TUTORIAL_MODULE_SO="$PWD/tutorial/module/mod_tutorial/build/mod_tutorial.so" \
docker compose -f tutorial/deploy/compose.yaml up --build
```

也可以使用教程 wrapper；它会在 `up` 前检查模块文件，并固定只操作
这个 Compose project：

```bash
TUTORIAL_ESL_PASSWORD='仅限隔离实验' \
TUTORIAL_MODULE_SO="$PWD/tutorial/module/mod_tutorial/build/mod_tutorial.so" \
bash tutorial/deploy/tutorial-compose.sh up
```

网站默认只绑定 `127.0.0.1:7009`；ESL 密码通过环境变量注入，不写入
公共配置或镜像层。结束时执行：

```bash
bash tutorial/deploy/tutorial-compose.sh down
```

重复执行 `up --build` 会复用同名 Compose project；`down` 只移除本
overlay 创建的容器和网络，不删除基线镜像、挂载的课程文件或证书。
若连同本教程生成的证书一起清理，使用：

```bash
bash tutorial/deploy/tutorial-compose.sh clean
```

排查启动顺序和模块加载时，使用：

```bash
bash tutorial/deploy/tutorial-compose.sh status
bash tutorial/deploy/tutorial-compose.sh logs
docker compose -f tutorial/deploy/compose.yaml exec freeswitch \
  /usr/local/freeswitch/bin/fs_cli -x 'show modules like mod_tutorial'
```

实验结束后，先保存需要的日志证据，再执行 `clean`；它只删除
`tutorial/deploy/certs/` 这一本教程生成的目录，不会触碰宿主机上
其他 FreeSWITCH 数据或配置。

## Optional Prometheus

Prometheus 不是阅读课程或拨打电话的前置条件。若需要抓取教学服务，
使用 [`prometheus.yml`](prometheus.yml)，它只请求本机的
`http://127.0.0.1:7009/metrics`，不会抓取 FreeSWITCH ESL：

```bash
prometheus --config.file=tutorial/deploy/prometheus.yml
```
