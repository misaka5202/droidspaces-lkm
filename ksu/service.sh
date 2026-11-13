#!/system/bin/sh
# Droidspaces LKM - 开机加载
#
# 在原始 GKI 内核上补齐 Droidspaces 所需的容器能力。
#
# 模块目录结构 (打包时生成):
#   ko/lkm4ctr-<kmi>.ko
#   ko/devtmpfs_lkm-<kmi>.ko
#   ko/macvlan-<kmi>.ko
#   ko/ip6table_nat-<kmi>.ko
#   ko/vkms-<kmi>.ko
#
# 加载顺序有讲究:
#   1) lkm4ctr       - PID/UTS/IPC namespace, Droidspaces 的硬性前提
#   2) devtmpfs_lkm  - 注册 devtmpfs (Droidspaces 靠 /proc/filesystems 判断)
#   3) ip6table_nat  - IPv6 NAT
#   4) macvlan       - 容器独立局域网 IP
#   5) vkms          - 虚拟 DRM 设备, 提供 /dev/dri/renderD128
#
# vkms 是 anland / wayland 桌面的前提:
#   Hyprland 和 KWin 的 Wayland 后端要一个能 gbm_create_device() 的
#   DRM render node。厂商内核没开 CONFIG_DRM_VGEM / CONFIG_DRM_VKMS,
#   只有 card0 (display controller), 没有 render 节点。
#
#   为什么是 vkms 而不是 vgem: Mesa 的 GBM 后端按 DRM driver 名字找 dri
#   驱动, 有 vkms_dri.so 却**没有** vgem_dri.so。用 vgem 时 Hyprland 停在
#     openRenderNode got drm device /dev/dri/renderD128
#     CRIT: Couldn't open a gbm device   (OpenGL.cpp:351)
#   vkms 是内核自带的虚拟 KMS 驱动 (card + render + CRTC + plane), 配
#   kms_swrast_dri.so 就能软件渲染出帧。
#
#   本仓库对 vkms 打了一个补丁: 上游 driver_features 只有
#   MODESET|ATOMIC|GEM, 不建 render node; 加上 DRIVER_RENDER 后才会生成
#   /dev/dri/renderD128。见 extra/vkms/Makefile 的说明。
#
#   必须在容器启动前加载, 否则 Droidspaces 扫描 /dev 时看不到它。
#
# devtmpfs_lkm 必须带 devtmpfs_probe=2:
#   默认 probe=0 会启动 kthread 并安装 device_add/device_del inline hook,
#   那套代码会改写内核只读段。实测在开机阶段用默认参数加载会导致卡开机。
#   probe=2 只注册文件系统, 不碰 hook —— 这正是 Droidspaces 需要的全部,
#   因为它只检查 /proc/filesystems 里有没有 devtmpfs。

MODDIR=${0%/*}
[ "$MODDIR" = "$0" ] && MODDIR=.
LOG="/data/local/tmp/droidspaces-lkm.log"

: > "$LOG"
echo "=== Droidspaces LKM $(date) ===" >> "$LOG"
echo "kernel:  $(uname -r)" >> "$LOG"
echo "moddir:  $MODDIR" >> "$LOG"

# 从内核版本推导 KMI 标签, 例如 android14-6.1
# 6.1.157-android14-11-o-g... -> android14-6.1
KREL=$(uname -r)
KMI=""
for tag in android12-5.10 android13-5.10 android13-5.15 android14-5.15 \
           android14-6.1 android15-6.6 android16-6.12 android17-6.18; do
    case "$KREL" in
        *"${tag%-*}"*"-${tag#*-}"*|*"-$tag"*) KMI="$tag"; break ;;
    esac
done
# 更宽松的匹配: 直接从版本串里取 androidN 与 major.minor
if [ -z "$KMI" ]; then
    android=$(echo "$KREL" | grep -oE 'android[0-9]+' | head -1)
    major=$(echo "$KREL" | grep -oE '^[0-9]+\.[0-9]+' | head -1)
    [ -n "$android" ] && [ -n "$major" ] && KMI="$android-$major"
fi
echo "kmi:     ${KMI:-未识别}" >> "$LOG"

# 选 ko: 优先精确 KMI, 否则退回目录里唯一的一组
find_ko() {
    local base="$1"
    local f="$MODDIR/ko/$base-$KMI.ko"
    if [ -n "$KMI" ] && [ -f "$f" ]; then echo "$f"; return 0; fi
    # 回退: 任意 KMI 版本
    local any
    any=$(ls "$MODDIR/ko/$base"-*.ko 2>/dev/null | head -1)
    [ -n "$any" ] && { echo "$any"; return 0; }
    return 1
}

load_mod() {
    local base="$1" params="$2"
    local ko
    if ! ko=$(find_ko "$base"); then
        echo "[MISS] $base (KMI=${KMI:-?})" >> "$LOG"
        return 1
    fi
    local name
    name=$(basename "$ko" .ko)

    # 判断是否已加载: 用模块名而不是文件名, 因为文件名带 KMI 后缀
    if grep -q "^${base} " /proc/modules 2>/dev/null; then
        echo "[SKIP] $base 已加载" >> "$LOG"
        return 0
    fi

    local out
    if [ -n "$params" ]; then
        out=$(insmod "$ko" $params 2>&1)
    else
        out=$(insmod "$ko" 2>&1)
    fi

    if [ $? -eq 0 ] && grep -q "^${base} " /proc/modules 2>/dev/null; then
        echo "[OK]   $base  ($(basename "$ko"))${params:+ $params}" >> "$LOG"
        return 0
    fi
    echo "[FAIL] $base: ${out:-无输出}" >> "$LOG"
    return 1
}

echo "--- 加载 ---" >> "$LOG"
load_mod lkm4ctr ""
load_mod devtmpfs_lkm "devtmpfs_probe=2"
load_mod ip6table_nat ""
load_mod macvlan ""
load_mod vkms ""

# cgroup 限流仿真（--cpus / --pids-limit）。
# 装 kprobe 并在运行时解析符号，最坏表现是加载失败；
# 放最后，失败不挡前面五个模块。
# 想临时关掉：在模块目录放 disable_pids / disable_cpu。
MODDIR="/data/adb/modules/droidspaces_lkm"
[ -f "$MODDIR/disable_pids" ] || load_mod droid_lkm_pids ""
[ -f "$MODDIR/disable_cpu" ] || load_mod droid_lkm_cpu ""

echo "--- lsmod ---" >> "$LOG"
lsmod | grep -E '^(lkm4ctr|devtmpfs_lkm|ip6table_nat|macvlan|vkms|droid_lkm_pids|droid_lkm_cpu)' >> "$LOG" 2>/dev/null

echo "--- 能力验证 ---" >> "$LOG"
unshare -pf true 2>/dev/null \
    && echo "[OK] PID namespace" >> "$LOG" \
    || echo "[FAIL] PID namespace" >> "$LOG"
unshare -i true 2>/dev/null \
    && echo "[OK] IPC namespace" >> "$LOG" \
    || echo "[FAIL] IPC namespace" >> "$LOG"
grep -q devtmpfs /proc/filesystems 2>/dev/null \
    && echo "[OK] devtmpfs" >> "$LOG" \
    || echo "[--] devtmpfs 未注册" >> "$LOG"
grep -q "^droid_lkm_pids " /proc/modules 2>/dev/null \
    && echo "[OK] pids 控制器仿真" >> "$LOG" \
    || echo "[--] pids 控制器仿真未加载" >> "$LOG"
grep -q "^droid_lkm_cpu " /proc/modules 2>/dev/null \
    && echo "[OK] cpu.max 仿真" >> "$LOG" \
    || echo "[--] cpu.max 仿真未加载" >> "$LOG"

# vkms 提供的 render node: anland / wayland 合成器的前提
if [ -e /dev/dri/renderD128 ]; then
    echo "[OK] /dev/dri/renderD128 (vkms)" >> "$LOG"
else
    echo "[--] /dev/dri/renderD128 缺失 (anland 桌面不可用)" >> "$LOG"
fi

echo "=== 完成 ===" >> "$LOG"
exit 0
