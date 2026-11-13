#!/bin/bash
# 在 DDK 容器里为本 KMI 构建全部模块。
#
# 核心原则: 每个 KMI 用**它自己内核树里的**驱动源码。
#
#   vkms / macvlan 都是内核树里的普通驱动文件, 它们的 API 随内核版本变化
#   (net_device 的字段、drm_gem_shmem 的接口、drmm_ 系列的引入时间...)。
#   拿 6.1 的副本去编 5.10 必然缺字段。DDK 的 kdir 是完整内核源码树,
#   直接用它 drivers/ 下的源码, 天然匹配该版本。
#
#   devtmpfs_lkm / ip6table_nat 是我们自己的实现(内核里没有可对应的独立
#   驱动), 跨版本失败可以接受, 记 warning 继续。
#
# 用法: build_modules.sh <KMI> <KDIR> <WORKSPACE> <OUT>
set -uo pipefail

KMI="${1:?KMI}"
KDIR="${2:?KDIR}"
WS="${3:?WORKSPACE}"
OUT="${4:?OUT}"
NPROC="$(nproc 2>/dev/null || echo 4)"

mkdir -p "$OUT"
FAILED=()

log() { echo ">>> $*"; }
warn() { echo "::warning::$*"; FAILED+=("$*"); }

# DDK 的 kdir 可能只有 vmlinux/symvers, 源码在别处; 两个位置都试
KSRC=""
for c in "$KDIR" "$KDIR/../src" "/opt/ddk/src/$KMI" "/opt/ddk/kernel/$KMI"; do
    if [ -d "$c/drivers/gpu/drm" ] || [ -d "$c/drivers/net" ]; then
        KSRC="$c"; break
    fi
done

log "KMI=$KMI"
log "KDIR=$KDIR"
if [ -n "$KSRC" ]; then
    log "源码树=$KSRC"
    log "  vkms:    $([ -d "$KSRC/drivers/gpu/drm/vkms" ] && echo 有 || echo 无)"
    log "  macvlan: $([ -f "$KSRC/drivers/net/macvlan.c" ] && echo 有 || echo 无)"
else
    warn "找不到内核源码树, 将退回仓库内副本"
fi

# 把一组 .c 编成 obj-m := <mod>.o 形式的 Makefile
gen_makefile() {
    local dir="$1" mod="$2" extra_cflags="${3:-}"
    {
        echo "obj-m := $mod.o"
        local objs
        objs="$(cd "$dir" && ls ./*.c 2>/dev/null | sed 's#^\./##; s#\.c$#.o#' | tr '\n' ' ')"
        echo "$mod-y := $objs"
        [ -n "$extra_cflags" ] && echo "ccflags-y += $extra_cflags"
    } > "$dir/Makefile"
}

# ---------------------------------------------------------------------------
# 1) lkm4ctr —— 上游已做跨版本适配, 直接用
# ---------------------------------------------------------------------------
log "=== lkm4ctr @ $KMI ==="
if make -C "$KDIR" M=/tmp/lkm4ctr/vendor LKM4CTR_LOG_DMESG=y modules -j"$NPROC"; then
    cp /tmp/lkm4ctr/vendor/lkm4ctr.ko "$OUT/lkm4ctr-$KMI.ko" && log "  [OK]"
else
    warn "lkm4ctr @ $KMI"
fi

# ---------------------------------------------------------------------------
# 2) vkms —— 内核树里的源码 + DRIVER_RENDER 补丁
#
# 上游 vkms 的 driver_features 没有 DRIVER_RENDER, drm_dev_register() 就
# 不会分配 render minor, 于是只有 card 没有 /dev/dri/renderD*。而 Wayland
# 合成器(gbm_create_device + drmOpenRender)要的正是 render node。
# ---------------------------------------------------------------------------
log "=== vkms @ $KMI ==="
DST=/tmp/mod-vkms
rm -rf "$DST"; mkdir -p "$DST"
if [ -n "$KSRC" ] && [ -d "$KSRC/drivers/gpu/drm/vkms" ]; then
    cp -a "$KSRC/drivers/gpu/drm/vkms/". "$DST/" 2>/dev/null || true
    # Makefile 由我们生成, 内核树里那份是 obj-$(CONFIG_DRM_VKMS) 形式
    rm -f "$DST/Makefile" "$DST/Kconfig"

    if ! grep -q 'DRIVER_RENDER' "$DST/vkms_drv.c" 2>/dev/null; then
        # 在 driver_features 那一行末尾追加 DRIVER_RENDER
        python3 - "$DST/vkms_drv.c" <<'PYEOF' 2>/dev/null || \
            sed -i 's/DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM/DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM | DRIVER_RENDER/' "$DST/vkms_drv.c"
import re, sys
p = sys.argv[1]
s = open(p, encoding='utf-8').read()
# .driver_features = A | B | C,   →  .driver_features = A | B | C | DRIVER_RENDER,
def repl(m):
    body = m.group(1).rstrip()
    if 'DRIVER_RENDER' in body:
        return m.group(0)
    return '.driver_features' + m.group(0)[len('.driver_features'):m.end(1)] + ' | DRIVER_RENDER,'
s2 = re.sub(r'\.driver_features\s*=\s*([^,\n}]+),', repl, s, count=1)
if s2 == s:                      # 没匹配到就退回最简单的替换
    s2 = s.replace('DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM',
                   'DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM | DRIVER_RENDER')
open(p, 'w', encoding='utf-8').write(s2)
PYEOF
    fi
    log "  driver_features: $(grep -h 'driver_features' "$DST/vkms_drv.c" 2>/dev/null | head -1 | tr -s ' ')"

    gen_makefile "$DST" vkms "-DCONFIG_DRM_VKMS=1"
    if make -C "$KDIR" M="$DST" modules -j"$NPROC"; then
        cp "$DST/vkms.ko" "$OUT/vkms-$KMI.ko" && log "  [OK]"
    else
        warn "vkms @ $KMI"
    fi
else
    warn "vkms @ $KMI (内核树无源码)"
fi

# ---------------------------------------------------------------------------
# 3) macvlan —— 内核树里的源码
# ---------------------------------------------------------------------------
log "=== macvlan @ $KMI ==="
DST=/tmp/mod-macvlan
rm -rf "$DST"; mkdir -p "$DST"
if [ -n "$KSRC" ] && [ -f "$KSRC/drivers/net/macvlan.c" ]; then
    cp "$KSRC/drivers/net/macvlan.c" "$DST/"
    # 6.1 及更早的 macvlan 需要 if_macvlan.h; 新版本已并入 uapi
    for h in include/linux/if_macvlan.h include/uapi/linux/if_macvlan.h; do
        [ -f "$KSRC/$h" ] && cp "$KSRC/$h" "$DST/"
    done
    gen_makefile "$DST" macvlan "-DCONFIG_MACVLAN=1"
    if make -C "$KDIR" M="$DST" modules -j"$NPROC"; then
        cp "$DST/macvlan.ko" "$OUT/macvlan-$KMI.ko" && log "  [OK]"
    else
        warn "macvlan @ $KMI"
    fi
else
    warn "macvlan @ $KMI (内核树无源码)"
fi

# ---------------------------------------------------------------------------
# 4) devtmpfs_lkm / ip6table_nat —— 仓库内实现
# ---------------------------------------------------------------------------
for m in devtmpfs net_ip6nat; do
    SRC="$WS/extra/$m"
    [ -f "$SRC/Makefile" ] || continue
    log "=== $m @ $KMI ==="
    find "$SRC" -name '*.o' -delete 2>/dev/null || true
    find "$SRC" -name '.*.cmd' -delete 2>/dev/null || true
    if make -C "$KDIR" M="$SRC" modules -j"$NPROC"; then
        case "$m" in
            devtmpfs)   cp "$SRC"/devtmpfs_lkm.ko "$OUT/devtmpfs_lkm-$KMI.ko" ;;
            net_ip6nat) cp "$SRC"/ip6table_nat.ko "$OUT/ip6table_nat-$KMI.ko" ;;
        esac
        log "  [OK]"
    else
        warn "$m @ $KMI"
    fi
    find "$SRC" -name '*.o' -delete 2>/dev/null || true
    find "$SRC" -name '.*.cmd' -delete 2>/dev/null || true
done

echo
echo "=== $KMI 产物 ==="
ls -lh "$OUT" 2>/dev/null
echo
if [ ${#FAILED[@]} -gt 0 ]; then
    echo "=== 本 KMI 失败项 (${#FAILED[@]}) ==="
    printf '  - %s\n' "${FAILED[@]}"
fi
# 不返回非 0: 部分模块失败不应中断整条矩阵, 其余 KMI 还应继续产出。
exit 0
