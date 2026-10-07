# Day 5 实验资产

软电话：UDP `5060`，用户 `1000`/`1001`，服务器为当前 `domain`。口令用 `global_getvar default_password` 读取。

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'sofia status profile internal reg'
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'show registrations'
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'show channels'
```

笔记只保留短 UUID 和听感结论。
