# Day 8

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'xml_locate dialplan context name default' | grep -m1 Local_Extension
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'show channels'
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'originate {ignore_early_media=true}loopback/8888/default &park'
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x hupall
```
