# Day 6

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'sofia profile internal siptrace on'
# 打一通 1000→1001 后立刻关闭
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'sofia profile internal siptrace off'
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'console loglevel notice'
```

笔记只保留方法与状态码。
