# 高通设备上跑 anland 桌面

调研结论，基于对 anland / Droidspaces / Desktop-builder 源码和 CI 的阅读。
**尚未在高通真机上实测完成**（设备在测试过程中断开）。

## 结论速览

| 项目 | 高通（kgsl/turnip） | 联发科（Mali，我们已实测） |
|------|-------------------|------------------------|
| render node | **原生就有** `/dev/dri/renderD128` | 需要 `vkms.ko` 补 |
| GPU 驱动 | Turnip（Mesa 的 Vulkan） | llvmpipe 软件渲染 |
| `DRI_PRIME` 类配置 | `MESA_LOADER_DRIVER_OVERRIDE=kgsl` | 必须删掉，改 `swrast` |
| 镜像适配 | **现有镜像已原生支持** | 需要手工改 `/etc/environment` |
| 预期性能 | 比 Mali 好（有真实 GPU 加速） | 142% CPU 软件渲染 |

**高通反而更简单** —— anland 的 KDE backend 注释直接写明了这条路：

```cpp
// anland_backend.cpp:70-71
 * backend does), then the standard render node — which on the kgsl/turnip stack
 * is the msm node exposed at /dev/dri/renderD128.
```

## 为什么高通不需要 vkms

`vkms.ko` 存在的唯一理由是：厂商内核只开了 `CONFIG_DRM_MALI_DISPLAY`
（或类似），创建了 `card0` 但没有 render minor。而 Wayland 合成器硬性要求
一个可读写的 render node。

高通设备上 Adreno 的 kgsl 驱动**本来就同时创建 card 和 render node**：

```
crw-rw-rw- root graphics 226,   0  card0        
crw-rw-rw- root graphics 226, 128  renderD128   ← 原生
```

所以 `vkms` 对高通**不仅没必要，还可能干扰**——它会额外创建一个
`card1` + `renderD129`，如果 `ANLAND_DRM_DEVICE` 没指对，合成器可能打开错的
那个设备。

### 建议

高通设备**不要加载 vkms**：

```bash
# 从 service.sh 里去掉这一行
# load_mod vkms ""
```

或者保留加载但确保 `ANLAND_DRM_DEVICE=/dev/dri/renderD128`（Adreno 那个）。

## 镜像从哪来：三层架构

这是「fork 改 CI 发放镜像」这个问题的核心答案。

```
第 1 层  droidspaces-package        (Goldzxcbug)
         └── build-anland-packages.yml
             产出: anland-hyprland-debian13-0.55.2+ds-arm64.tar.gz (5MB)
                   anland-hyprland-ubuntu2604-...tar.gz (6MB)
                   ...
             发布: Release tag = anland-hyprland-packages

第 2 层  Droidspaces-rootfs-Desktop-builder  (Goldzxcbug)
         ├── build-rootfs-all-wayland.yml   调度器（矩阵、cron）
         ├── build-rootfs-core.yml          可复用 workflow（真正干活）
         │    ├── docker buildx 用 *.Dockerfile
         │    ├── Dockerfile 里 install-anland-desktop 拉第 1 层的包
         │    └── upload-artifact → softprops/action-gh-release
         └── build-rootfs-releases{,-en}.yml
             产出: Debian-13-hyprland-Wayland-...-rootfs-aarch64-...tar.xz

第 3 层  Droidspaces-OSS
         └── 只有 ci.yml（测编译），**不产镜像**
```

**关键点：anland 组件和 rootfs 镜像是分开发布的。** 如果你想改 anland
本身（比如给 Mali 打补丁），不用重建镜像，只发第 1 层的包即可。

## 现有镜像对高通的支持情况

`Debian-13.Dockerfile` 里这段就是给高通的：

```dockerfile
RUN if [ "$ENABLE_mesa_ARG" = "true" ] && [ "$DISPLAY_BACKEND" = "anland-wayland" ]; then \
        echo "MESA_LOADER_DRIVER_OVERRIDE=kgsl" >> /etc/environment; \
        echo "GALLIUM_DRIVER=kgsl" >> /etc/environment; \
        echo "FD_FORCE_KGSL=1" >> /etc/environment; \
    fi

# 修复骁龙8 Gen 2 设备在 Wayland 下的花屏问题
RUN if [ "$ENABLE_8gen2_wayland_ARG" = "true" ]; then \
        echo "FD_DEV_FEATURES=enable_tp_ubwc_flag_hint=1" >> /etc/environment; \
    fi
```

`build-rootfs-all-wayland.yml` 传的参数：

```yaml
enable_mesa: true              # → kgsl 三行会写入，高通开箱可用
enable_8gen2_wayland: false    # → 花屏修复默认关
```

**所以现成的镜像直接就能在高通上跑，不需要改任何东西。**

> 这三行 kgsl 配置正是我给 Find X7 删掉的东西 —— 删它是对的（Mali 上没有
> kgsl 节点），但它本来就不是错的，只是写给高通用的。「一个镜像通吃所有
> SoC」这件事本身不成立。

## 如果要多设备通用：两个方案

### 方案 A：运行时探测（推荐，改动最小）

在 rootfs 里加一个启动脚本，按 GPU 类型决定用哪套配置：

```sh
#!/bin/sh
# /usr/local/bin/detect-gpu-env.sh
if [ -e /dev/kgsl-3d0 ]; then
    # 高通 Adreno
    echo "MESA_LOADER_DRIVER_OVERRIDE=kgsl" >> /etc/environment
    echo "GALLIUM_DRIVER=kgsl"          >> /etc/environment
    echo "FD_FORCE_KGSL=1"              >> /etc/environment
elif ls /dev/mali0 >/dev/null 2>&1; then
    # ARM Mali
    echo "MESA_LOADER_DRIVER_OVERRIDE=swrast" >> /etc/environment
    echo "LIBGL_ALWAYS_SOFTWARE=1"            >> /etc/environment
fi
```

在 Dockerfile 里改成无条件写这个脚本 + 在 `start-desktop-session` 里调用。
**好处：一个镜像通吃，不用维护两套。**

### 方案 B：fork 后加矩阵维度

在 `build-rootfs-all-wayland.yml` 的 `with:` 里加一个 `gpu_vendor` 参数：

```yaml
strategy:
  matrix:
    gpu: [qualcomm, mali]
with:
  gpu_vendor: ${{ matrix.gpu }}
```

`build-rootfs-core.yml` 里加 `gpu_vendor` 输入，传给 Dockerfile：

```dockerfile
RUN case "$GPU_VENDOR" in \
        qualcomm) echo "MESA_LOADER_DRIVER_OVERRIDE=kgsl"  > /etc/environment ;; \
        mali)     echo "MESA_LOADER_DRIVER_OVERRIDE=swrast" > /etc/environment ;; \
    esac
```

**代价：镜像数量翻倍（每个发行版 × 2），Release 体积也翻倍。**

### 我的建议

**方案 A**。理由：

1. 现有 `ENABLE_mesa` 开关已经在写 kgsl 了，加个探测脚本成本很低
2. 镜像数量不翻倍（现在 20 个资产已经不小）
3. 用户不需要知道自己是什么 SoC —— 这正是「一个包通吃」应该做的事
4. 探测条件很简单（`/dev/kgsl-3d0` vs `/dev/mali0`），不容易出错

## 高通设备实测清单

设备：Redmi `2410CRP4CG` / `uke` / `pineapple`（骁龙 8 Gen 3，SM8650）

```
内核       6.1.138-android14-11   → KMI android14-6.1
GPU        adreno, /dev/kgsl-3d0
render node /dev/dri/renderD128（原生）
Root       KernelSU（/system/bin/su）
```

### 已确认

- ✅ `droidspaces check` → **All required features found!**
  （`lkm4ctr` 补的 PID/IPC namespace 生效，`devtmpfs_lkm` 的 devtmpfs 生效）
- ✅ 5 个 LKM 中 4 个加载正常：`lkm4ctr` / `devtmpfs_lkm` / `ip6table_nat` / `vkms`
- ✅ `anland-daemon` 模块和 `com.anland.consumer` 都已安装

### 待验证

- ⬜ 创建容器 + 跑 Hyprland
- ⬜ anland 握手（producer/consumer）
- ⬜ Turnip GPU 加速实际效果
- ⬜ `macvlan` 加载（见下）

## macvlan 在这台设备上加载失败

```
$ insmod macvlan-android14-6.1.ko
insmod: failed: No such file or directory

$ dmesg
macvlan: Unknown symbol dev_uc_add_excl (err -2)
macvlan: Unknown symbol dev_mc_add_excl (err -2)
macvlan: Unknown symbol __dev_forward_skb (err -2)
macvlan: Unknown symbol ip_check_defrag (err -2)
macvlan: Unknown symbol ndo_dflt_fdb_dump (err -2)
```

**原因：`CONFIG_TRIM_UNUSED_KSYMS=y`。** 厂商内核没用到这 5 个符号，就把
它们的 `EXPORT_SYMBOL` 条目裁掉了。

关键：**这 5 个符号本身还在内核里**（`/proc/kallsyms` 能查到），只是没导出：

```bash
$ grep -cw dev_uc_add_excl /proc/kallsyms
1
```

### 修法：复用 KallRecon

`extra/net_ip6nat/kr/` 里那套 `KallRecon` 就是解决这个问题的框架 —— 它能从
`kallsyms` 里恢复地址，再用 `-include` 宏把调用重定向到 shim。`ip6table_nat`
已经用它自解析了 9 个符号：

```
[ip6t-nat-shim] self-resolved 9/9 symbols
```

给 macvlan 套同样的做法即可。需要 shim 的 5 个函数签名：

```c
int  dev_uc_add_excl(struct net_device *dev, const unsigned char *addr);
int  dev_mc_add_excl(struct net_device *dev, const unsigned char *addr);
int  __dev_forward_skb(struct net_device *dev, struct sk_buff *skb);
int  ip_check_defrag(struct net *net, struct sk_buff *skb, u8 user);
void ndo_dflt_fdb_dump(struct sk_buff *skb, struct netlink_callback *cb,
                       struct net_device *dev, struct net_device *filter_dev,
                       int *idx);
```

`macvlan` 是 `--net=macvlan` 才需要的可选能力，缺它不影响容器启动和桌面显示。

## 待办

1. 设备连回后按下面的顺序实测 anland
2. 如果 Turnip 跑通，考虑给 `droidspaces-lkm` 的 `service.sh` 加高通分支
   （高通不加载 vkms）
3. 给 macvlan 套 KallRecon（可选）

### 实测步骤

```bash
# 1) 解压 rootfs
su -c 'mkdir -p /data/local/Droidspaces/rootfs-hypr'
su -c 'cd /data/local/Droidspaces/rootfs-hypr && busybox tar xJf /sdcard/Download/qc-rootfs.tar.xz'

# 2) 高通上不要加载 vkms，避免多出一个 renderD129
su -c 'rmmod vkms'

# 3) 建容器
su -c '/data/local/Droidspaces/bin/droidspaces --name=qc \
    --rootfs=/data/local/Droidspaces/rootfs-hypr \
    --anland --gpu --net=host --init=/usr/lib/systemd/systemd start'

# 4) 验证 socket 一致（inode 必须相同）
S=$(su -c 'ls /data/local/Droidspaces/Net/anland-*.sock | head -1')
su -c "stat -c '%F inode=%i' $S"
su -c "stat -c '%F inode=%i' /data/local/Droidspaces/rootfs-hypr/run/display.sock"

# 5) 起 Hyprland
su -c '/data/local/Droidspaces/bin/droidspaces --name=qc run -- /bin/sh /root/hnx.sh'

# 6) 连 app（KernelSU 下同样需要 anlandproxy 转发）
su -c 'nohup /data/local/tmp/anlandproxy /data/local/tmp/anland_bridge.sock "$S" &'
su -c 'am start -n com.anland.consumer/.MainActivity'

# 7) 看 daemon 日志确认握手
su -c 'tail -f /data/local/Droidspaces/Logs/qc/anland'
```

期望看到：

```
daemon: producer connected
daemon: consumer connected
daemon: fds delivered to producer
daemon: screen info <宽>x<高> fmt=1
```

然后 `hyprctl monitors` 应该显示 `ANLAND-1`，`GL_RENDERER` 应该是
Turnip/Adreno 而不是 llvmpipe。
