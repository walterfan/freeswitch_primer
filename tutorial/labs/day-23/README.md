# Day 23

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
  -x 'uuid_broadcast <UUID> tutorial_ivr_metric::main^^1 aleg'
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'uuid_getvar <UUID> tutorial_ivr_recorded'
```
