# Day 26

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x status
curl -fsS http://127.0.0.1:7009/metrics | grep -E 'sessions|calls|heartbeat' || true
```
