cmake -S primer/snippet \
  -B primer/snippet/build \
  -DFREESWITCH_PC_FILE="$HOME/fs/lib/pkgconfig/freeswitch.pc"

cmake --build primer/snippet/build