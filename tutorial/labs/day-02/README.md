# Day 2 实验资产

远端只读检查：

```bash
ssh walter@10.100.212.8
docker ps --filter name=freeswitch
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x status
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x "sofia status profile internal"
docker logs --tail 100 freeswitch
```

端口可达性不等于协议或媒体验收。日志与命令输出分享前必须脱敏。
