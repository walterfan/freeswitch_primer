# Day 22

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'tutorial_metrics'
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'tutorial_metrics json'
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'tutorial_metrics xml'
```

后一条应 USAGE，且不增加 invocations。
