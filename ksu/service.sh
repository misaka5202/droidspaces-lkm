#!/system/bin/sh
# lkm4ctr 开机加载 - 适配 OPPO Find X7 (android14-6.1)
#
# 单 KMI 简化版: ko 就在模块目录里, 不需要 KMI 选择逻辑。
#
# 设备实测结论 (2026-11-12):
#   模块加载后 Droidspaces 官方 check 显示 [MUST HAVE] 全部通过:
#     PID namespace   ✓   <- 由本模块补齐 (原 CONFIG_PID_NS=n)
#     IPC namespace   ✓   <- 由本模块补齐 (原 CONFIG_IPC_NS 实际为 y)
#     UTS / Mount namespace ✓
#
# 仍未覆盖 (Droidspaces 标为 OPTIONAL/RECOMMENDED, 不影响启动):
#   CONFIG_DEVTMPFS      -> 硬件访问模式不可用, 自动回退 tmpfs
#   CONFIG_MACVLAN       -> --net=macvlan 不可用
#   CONFIG_IP6_NF_NAT    -> NAT 容器仅 IPv4
#   CONFIG_CFS_BANDWIDTH -> --cpus 被跳过
#   CONFIG_CGROUP_PIDS   -> --pids-limit 被跳过
#   CONFIG_USER_NS       -> --allow-sandboxing 不可用

MODDIR=${0%/*}
[ "$MODDIR" = "$0" ] && MODDIR=.
LOG="/data/local/tmp/lkm4ctr.log"

: > "$LOG"
echo "=== lkm4ctr load $(date) ===" >> "$LOG"
echo "kernel: $(uname -r)" >> "$LOG"

KO="$MODDIR/lkm4ctr.ko"
if [ ! -f "$KO" ]; then
    echo "[FAIL] ko 不存在: $KO" >> "$LOG"
    exit 1
fi

# 已加载则跳过, 避免重复 insmod
if grep -q '^lkm4ctr ' /proc/modules 2>/dev/null; then
    echo "[SKIP] 已加载" >> "$LOG"
    exit 0
fi

# 先尝试正常加载
if out=$(insmod "$KO" 2>&1); then
    echo "[OK] insmod 成功" >> "$LOG"
else
    echo "[WARN] insmod 失败: $out" >> "$LOG"
    # 以下按需放开。本机实测无需任何参数即加载成功,
    # 说明 DDK 编出的模块 ABI 与设备一致 (struct module 1088 字节匹配)。
    # if out2=$(insmod "$KO" gate=0 2>&1); then
    #     echo "[OK] 带 gate=0 加载成功" >> "$LOG"
    # else
    #     echo "[FAIL] $out2" >> "$LOG"
    #     exit 1
    # fi
fi

# 记录加载结果
echo "--- lsmod ---" >> "$LOG"
lsmod | grep -E '^lkm4ctr' >> "$LOG" 2>/dev/null

# 验证 namespace 能力是否生效
echo "--- namespace 验证 ---" >> "$LOG"
if unshare -pf true 2>/dev/null; then
    echo "[OK] PID namespace 可用" >> "$LOG"
else
    echo "[FAIL] PID namespace 不可用" >> "$LOG"
fi
if unshare -i true 2>/dev/null; then
    echo "[OK] IPC namespace 可用" >> "$LOG"
else
    echo "[FAIL] IPC namespace 不可用" >> "$LOG"
fi
