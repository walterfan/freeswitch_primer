# Day 18

- IVR：`tutorial/module/mod_tutorial/tutorial-ivr.xml`
- default 入口（可选）：[9000-transfer.xml](9000-transfer.xml)

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x reloadxml
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'originate {ignore_early_media=true}loopback/9000@tutorial &park'
```
