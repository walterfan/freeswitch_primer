# Day 1 实验资产

本课直接使用已搭建的 Ubuntu/Debian FreeSWITCH 容器和教学服务，不修改运行配置。

只读检查：

```bash
docker ps --filter name=freeswitch
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x status
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x "sofia status"
curl -fsS http://127.0.0.1:7009/api/v1/health
```

演示密码、ESL 密码、SIP Authorization、私钥、完整号码与地址不得写入实验记录。
