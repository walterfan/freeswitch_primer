# Day 9

实用分机：`9196` echo，`9197` milliwatt，`5000` demo IVR（可能缺声音包）。

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'show channels'
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'sofia status profile internal'
```

`uuid_dump` 只抄 codec 名。
