# Day 7

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'user_exists id 1000 10.100.212.8'
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'user_data 1000@10.100.212.8 var user_context'
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'sofia status profile internal reg'
```

不要运行会打印密码的 `find_user_xml`。
