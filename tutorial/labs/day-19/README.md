# Day 19

```bash
nc -vz -w 2 10.100.212.8 8021 || true
curl -fsS http://127.0.0.1:7009/api/v1/public-config
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x status
```

远程 8021 不通是正确边界。
