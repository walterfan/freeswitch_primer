# Day 21

```bash
cmake -S tutorial/module/mod_tutorial -B tutorial/module/mod_tutorial/build \
  -DFREESWITCH_PC_FILE="$HOME/fs/lib/pkgconfig/freeswitch.pc"
cmake --build tutorial/module/mod_tutorial/build
ctest --test-dir tutorial/module/mod_tutorial/build --output-on-failure
```
