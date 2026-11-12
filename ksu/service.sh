#!/system/bin/sh
# lkm4ctr + 额外模块 开机加载 - 适配 OPPO Find X7 (android14-6.1)
#
# 设备实测结论 (2026-11-12):
#   lkm4ctr 加载后 Droidspaces 官方 check 显示 [MUST HAVE] 全部通过:
#     PID namespace / IPC namespace / UTS namespace / Mount namespace
#   容器实测可启动: Alpine 3.20 内 pid=1 uid=0, apk 可用。
#
# 未覆盖项 (Droidspaces 标为 OPTIONAL/RECOMMENDED, 不影响启动):
#   CONFIG_DEVTMPFS      -> 硬件访问模式不可用, 自动回退 tmpfs
#   CONFIG_MACVLAN       -> --net=macvlan 不可用
#   CONFIG_IP6_NF_NAT    -> NAT 容器仅 IPv4
#   CONFIG_CFS_BANDWIDTH -> --cpus 被跳过
#   CONFIG_CGROUP_PIDS   -> --pids-limit 被跳过
#   CONFIG_USER_NS       -> --allow-sandboxing 不可用
#
# 额外模块 (macvlan.ko / ip6table_nat.ko) 来自 ai-luo2006/droidspaces-6.12-pmb110,
# 若能编译成功则一并加载, 失败不影响主模块。

MODDIR=${0%/*}
[ "$MODDIR" = "$0" ] && MODDIR=.
LOG="/data/local/tmp/lkm4ctr.log"

: > "$LOG"
echo "=== lkm4ctr load $(date) ===" >> "$LOG"
echo "kernel: $(uname -r)" >> "$LOG"

loaded_any=0

# 加载顺序有讲究:
#   1) lkm4ctr      - 提供 namespace, 是 Droidspaces 的硬性前提
#   2) ip6table_nat - netfilter 模块, 依赖内核已有的 nf_nat 等符号
#   3) macvlan      - 网络设备驱动
#
# 每个都是独立的, 任何一个失败不影响其他, 也不影响开机。
for ko in "$MODDIR"/lkm4ctr.ko "$MODDIR"/devtmpfs_lkm.ko "$MODDIR"/ip6table_nat.ko "$MODDIR"/macvlan.ko; do
    [ -f "$ko" ] || continue
    name=$(basename "$ko" .ko)

    # 已加载则跳过
    if grep -q "^${name} " /proc/modules 2>/dev/null; then
        echo "[SKIP] $name 已加载" >> "$LOG"
        continue
    fi

    if out=$(insmod "$ko" 2>&1); then
        echo "[OK]   $name" >> "$LOG"
        loaded_any=1
    else
        echo "[FAIL] $name: $out" >> "$LOG"
    fi
done

# 兼容: 目录里还可能有其他命名
for ko in "$MODDIR"/*.ko; do
    [ -f "$ko" ] || continue
    name=$(basename "$ko" .ko)
    case "$name" in
        lkm4ctr|macvlan|ip6table_nat) continue ;;
    esac
    grep -q "^${name} " /proc/modules 2>/dev/null && continue
    if out=$(insmod "$ko" 2>&1); then
        echo "[OK]   $name" >> "$LOG"
    else
        echo "[FAIL] $name: $out" >> "$LOG"
    fi
done

echo "--- lsmod ---" >> "$LOG"
lsmod | grep -E '^(lkm4ctr|macvlan|ip6table_nat)' >> "$LOG" 2>/dev/null

# 验证 namespace 能力
echo "--- namespace 验证 ---" >> "$LOG"
unshare -pf true 2>/dev/null \
    && echo "[OK] PID namespace 可用" >> "$LOG" \
    || echo "[FAIL] PID namespace 不可用" >> "$LOG"
unshare -i true 2>/dev/null \
    && echo "[OK] IPC namespace 可用" >> "$LOG" \
    || echo "[FAIL] IPC namespace 不可用" >> "$LOG"

# 网络模块状态 (供 Droidspaces 判断 macvlan / NAT66 是否可用)
echo "--- 网络模块 ---" >> "$LOG"
for m in devtmpfs_lkm macvlan ip6table_nat; do
    if grep -q "^${m} " /proc/modules 2>/dev/null; then
        echo "[OK] $m" >> "$LOG"
    else
        echo "[--] $m 未加载 (对应功能不可用, 不影响容器启动)" >> "$LOG"
    fi
done

exit 0
