# 共享实验命令

隔离 lab 默认：Ubuntu 主机 `10.100.212.8`，容器名 `freeswitch`，安装前缀 `/usr/local/freeswitch`。教学网站在 MacBook 的 `127.0.0.1:7009`。不要把 ESL `8021` 暴露到不可信网络。

```bash
FS_CLI='docker exec freeswitch /usr/local/freeswitch/bin/fs_cli'
$FS_CLI -x status
$FS_CLI -x 'sofia status profile internal'
$FS_CLI -x 'sofia status profile internal reg'
$FS_CLI -x 'show registrations'
$FS_CLI -x 'show channels'
$FS_CLI -x 'console loglevel notice'
```

演示 SIP 口令只在隔离环境使用。需要登录软电话时，在容器内读取，不要写入课程笔记：

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'global_getvar default_password'
```

临时 SIP 跟踪必须成对关闭：

```bash
$FS_CLI -x 'sofia profile internal siptrace on'
# 收集方法、状态码、阶段后立刻：
$FS_CLI -x 'sofia profile internal siptrace off'
$FS_CLI -x 'console loglevel notice'
```

分享任何输出前删除 Authorization、Digest、密码、完整 UUID、原始 SDP、主机地址和号码。
