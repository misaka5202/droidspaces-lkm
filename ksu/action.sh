#!/system/bin/sh
# Action 按钮: 显示当前加载状态和 namespace 能力

MODDIR=${0%/*}
[ "$MODDIR" = "$0" ] && MODDIR=.

echo "=== lkm4ctr 状态 ==="
if grep -q '^lkm4ctr ' /proc/modules 2>/dev/null; then
    echo "已加载:"
    lsmod | grep '^lkm4ctr'
else
    echo "未加载"
fi

echo
echo "=== namespace 能力 ==="
for t in p i U u; do
    case "$t" in
        p) n="PID" ;;
        i) n="IPC" ;;
        U) n="USER" ;;
        u) n="UTS" ;;
    esac
    if unshare -$t true 2>/dev/null; then
        echo "  [OK]   $n"
    else
        echo "  [FAIL] $n"
    fi
done

echo
echo "=== 加载日志 ==="
tail -20 /data/local/tmp/lkm4ctr.log 2>/dev/null || echo "(无日志)"
