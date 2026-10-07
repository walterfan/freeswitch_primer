# Day 10

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'global_getvar local_ip_v4'
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'sofia status profile internal'
```

pcap 实验结束即删。
