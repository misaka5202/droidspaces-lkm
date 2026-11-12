#!/system/bin/sh
# Action 按钮: 显示加载状态和能力验证

MODDIR=${0%/*}
[ "$MODDIR" = "$0" ] && MODDIR=.

echo "=== Droidspaces LKM 状态 ==="
if lsmod | grep -qE '^(lkm4ctr|devtmpfs_lkm)'; then
    lsmod | grep -E '^(lkm4ctr|devtmpfs_lkm|ip6table_nat|macvlan)'
else
    echo "未加载"
fi

echo
echo "=== 能力 ==="
unshare -pf true 2>/dev/null && echo "  [OK]   PID namespace" || echo "  [FAIL] PID namespace"
unshare -i  true 2>/dev/null && echo "  [OK]   IPC namespace" || echo "  [FAIL] IPC namespace"
grep -q devtmpfs /proc/filesystems 2>/dev/null \
    && echo "  [OK]   devtmpfs" \
    || echo "  [--]   devtmpfs 未注册"

echo
echo "=== 日志 ==="
tail -25 /data/local/tmp/droidspaces-lkm.log 2>/dev/null || echo "(无日志)"
