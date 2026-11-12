#!/system/bin/sh
# 卸载时不主动 rmmod。
# 容器或进程可能正持有 vendored namespace, 强行卸载会导致内核 panic。
# 模块会驻留到下次重启。

rm -f /data/local/tmp/lkm4ctr.log
