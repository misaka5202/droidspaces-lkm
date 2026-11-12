#!/system/bin/sh
# lkm4ctr + 额外模块 开机加载 - 适配 OPPO Find X7 (android14-6.1)
#
# 设备: 6.1.157-android14-11-o-g2d576b643492
#
# 实测结论 (2026-11-12):
#   Droidspaces v7.2.0 check -> [MUST HAVE] 全部通过
#   devtmpfs / Macvlan / IPv6 NAT 三项从 ✗ 变 ✓
#   容器实测: Alpine 3.20 启动成功, pid=1 uid=0, apk 可用
#   硬件访问: /dev/mali0 已映射进容器, open 成功
#
# 加载顺序有讲究:
#   1) lkm4ctr       - 提供 PID/UTS/IPC namespace, Droidspaces 的硬性前提
#   2) devtmpfs_lkm  - 注册 devtmpfs 文件系统 (Droidspaces 靠 /proc/filesystems 判断)
#   3) ip6table_nat  - IPv6 NAT
#   4) macvlan       - 容器独立局域网 IP
#
# 每个都是独立的, 任何一个失败只记录日志, 不影响开机也不影响其他模块。
#
# devtmpfs_lkm 默认带 devtmpfs_probe=2 加载:
#   该参数表示"只注册文件系统, 不启动 kthread 和设备 hook"。
#   这正是 Droidspaces 需要的 —— 它只检查 /proc/filesystems 里有没有 devtmpfs,
#   不需要节点自动创建。不开 hook 可以避免在内核版本不匹配时出问题。
#   实测 probe=2 模式下 insmod 零报错, /proc/filesystems 立即出现 devtmpfs。

MODDIR=${0%/*}
[ "$MODDIR" = "$0" ] && MODDIR=.
LOG="/data/local/tmp/lkm4ctr.log"

: > "$LOG"
echo "=== lkm4ctr load $(date) ===" >> "$LOG"
echo "kernel: $(uname -r)" >> "$LOG"
echo "moddir: $MODDIR" >> "$LOG"

loaded_any=0

# 带参数加载的模块单独处理; 参数用空格分隔的字符串给出
load_mod() {
    local name="$1"
    local params="$2"
    local ko="$MODDIR/$name.ko"

    if [ ! -f "$ko" ]; then
        echo "[MISS] $name.ko 不存在" >> "$LOG"
        return 1
    fi

    if grep -q "^${name} " /proc/modules 2>/dev/null; then
        echo "[SKIP] $name 已加载" >> "$LOG"
        return 0
    fi

    local out rc
    if [ -n "$params" ]; then
        out=$(insmod "$ko" $params 2>&1)
    else
        out=$(insmod "$ko" 2>&1)
    fi
    rc=$?

    if [ $rc -eq 0 ] && grep -q "^${name} " /proc/modules 2>/dev/null; then
        echo "[OK]   $name${params:+ ($params)}" >> "$LOG"
        loaded_any=1
        return 0
    else
        echo "[FAIL] $name: ${out:-无输出}" >> "$LOG"
        return 1
    fi
}

echo "--- 加载 ---" >> "$LOG"
load_mod lkm4ctr ""
load_mod devtmpfs_lkm "devtmpfs_probe=2"
load_mod ip6table_nat ""
load_mod macvlan ""

# 目录里其它未列出的 ko 也尝试加载（未来扩展）
for ko in "$MODDIR"/*.ko; do
    [ -f "$ko" ] || continue
    n=$(basename "$ko" .ko)
    case "$n" in
        lkm4ctr|devtmpfs_lkm|ip6table_nat|macvlan) continue ;;
    esac
    grep -q "^${n} " /proc/modules 2>/dev/null && continue
    if out=$(insmod "$ko" 2>&1); then
        echo "[OK]   $n" >> "$LOG"
    else
        echo "[FAIL] $n: $out" >> "$LOG"
    fi
done

echo "--- lsmod ---" >> "$LOG"
lsmod | grep -E '^(lkm4ctr|devtmpfs_lkm|ip6table_nat|macvlan)' >> "$LOG" 2>/dev/null

# 验证关键能力
echo "--- 能力验证 ---" >> "$LOG"

unshare -pf true 2>/dev/null \
    && echo "[OK] PID namespace" >> "$LOG" \
    || echo "[FAIL] PID namespace" >> "$LOG"

unshare -i true 2>/dev/null \
    && echo "[OK] IPC namespace" >> "$LOG" \
    || echo "[FAIL] IPC namespace" >> "$LOG"

if grep -q devtmpfs /proc/filesystems 2>/dev/null; then
    echo "[OK] devtmpfs 已注册" >> "$LOG"
else
    echo "[FAIL] devtmpfs 未注册" >> "$LOG"
fi

# userns: 新版 lkm4ctr 会补出 /proc/<pid>/ns/user
if [ -e /proc/self/ns/user ]; then
    echo "[OK] userns (procfs 条目存在)" >> "$LOG"
else
    echo "[--] userns (无 procfs 条目)" >> "$LOG"
fi

echo "=== 完成 ===" >> "$LOG"
exit 0
