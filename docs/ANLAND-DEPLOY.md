# anland Linux 桌面部署手册

在 ** (MT6989 / Mali-G720)** 上运行 Linux 桌面，
画面通过 anland 显示在 Android 屏幕上。

本文档记录的是**实际验证过的步骤**，每一条都有对应的实测证据。
适用前提：不想重编内核，只能靠 LKM 补能力。

---

## 最终效果

```
Android 屏幕显示:
  ┌─────────────────────────┐
  │ root@hypr:~# █          │   ← Linux 终端
  │                         │
  │   (foot 窗口 590x1217)   │
  └─────────────────────────┘
  ESC  /  -  HOME ↑ END PGUP ⌨
  TAB CTRL ALT ← ↓ → PGDN ⚙    ← anland 键盘条
```

已验证的渲染方式：
| 方式 | GL_RENDERER | FPS (glmark2 640x480) |
|------|-------------|:---:|
| Mesa llvmpipe | `llvmpipe (LLVM 21.1.8, 128 bits)` | 23~27 |
| **MobileGL → Mali** | **`Espryt (MobileGL Core) (Mali-G720-Immortalis MC12, OpenGL ES 3.2)`** | 21~26 |

两者 FPS 接近，因为瓶颈在 anland 的软件合成而非 GPU 计算。
MobileGL 的价值在于**功能通路已验证**，可承载 OpenGL 4.6 程序。

---

## 组成部分

```
Android 侧
├── droidspaces-lkm 模块     5 个 ko：容器能力 + render node
├── anland-daemon 模块       显示 broker
├── com.anland.consumer      显示端 APK
├── anlandproxy              root 连接转发（绕开 KernelSU su 限制）
└── libMobileGLServer.so     Mali GPU 渲染服务（可选，GPU 加速用）

容器内（Droidspaces）
├── Hyprland 0.55.2          Wayland 合成器（anland backend）
├── /opt/mobilegl/           MobileGL 客户端（glibc aarch64）
└── /run/display.sock        anland socket
```

---

## 前置：LKM 模块

刷入 `droidspaces-lkm-android14-6.1-arm64.zip`（或全 KMI 版
`droidspaces-lkm-arm64.zip`），重启。

开机日志 `/data/local/tmp/droidspaces-lkm.log` 应显示：

```
[OK]   lkm4ctr  (lkm4ctr-android14-6.1.ko)
[OK]   devtmpfs_lkm  (devtmpfs_lkm-android14-6.1.ko) devtmpfs_probe=2
[OK]   ip6table_nat  (ip6table_nat-android14-6.1.ko)
[OK]   macvlan  (macvlan-android14-6.1.ko)
[OK]   vkms  (vkms-android14-6.1.ko)
[OK] PID namespace
[OK] IPC namespace
[OK] devtmpfs
[OK] /dev/dri/renderD128 (vkms)
```

**`vkms` 是关键。** 厂商内核没开 `CONFIG_DRM_VKMS`，只有 `card0`
（显示控制器）而没有 render node。Hyprland / KWin 的 Wayland 后端
硬性要求一个可读写的 DRM render node：

```c
// hyprland-anland 里的检查
[[ -r "$ANLAND_DRM_DEVICE" && -w "$ANLAND_DRM_DEVICE" ]] || die 'render node unavailable'
```

`vkms.ko` 加载后：

```
crw-rw-rw- 1 root graphics 226,  1  card1        ← vkms 显示设备
crw-rw-rw- 1 root graphics 226, 128  renderD128   ← render node
```

### 为什么是 vkms 而不是 vgem

Mesa 的 GBM 后端按 DRM driver 名字找 dri 驱动，完整安装的 Mesa 里有
`vkms_dri.so` 却**没有** `vgem_dri.so`。用 vgem 时 Hyprland 会停在：

```
DEBUG: openRenderNode got drm device /dev/dri/renderD128
CRIT : Couldn't open a gbm device            (OpenGL.cpp:351)
```

### vkms 需要打的补丁

上游 vkms 的 `driver_features` 只有 `MODESET|ATOMIC|GEM`，
`drm_dev_register()` 不会为它创建 render minor。补上 `DRIVER_RENDER`：

```c
// extra/vkms/vkms_drv.c
.driver_features = DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM |
                   DRIVER_RENDER,
```

---

## 第二步：anland 组件

```bash
# daemon（KernelSU 模块）
刷 anland-daemon.zip，或手动解压到 /data/adb/modules/anland-daemon/

# 显示端 APK
adb install anland-v5.apk
```

手动启动 daemon（调试时）：

```bash
su -c 'nohup /data/adb/modules/anland-daemon/display_daemon \
       /data/local/tmp/display_daemon.sock > /data/local/tmp/anland-daemon.log 2>&1 &'
```

---

## 第三步：容器

### 3.1 准备 rootfs

用 `Goldzxcbug/Droidspaces-rootfs-Desktop-builder` 的
**Ubuntu-26-hyprland-Wayland** 版：

```bash
# 解压到 rootfs 目录
su -c 'cd /data/local/Droidspaces/rootfs-ubuntu && \
       busybox tar xJf /sdcard/Download/Ubuntu-26-hyprland-*.tar.xz'
```

### 3.2 修复 rootfs 的 GPU 配置（关键）

这个 rootfs 是给高通 Adreno 预配置的，`/etc/environment` 里有三行致命配置：

```ini
MESA_LOADER_DRIVER_OVERRIDE=kgsl
GALLIUM_DRIVER=kgsl
FD_FORCE_KGSL=1
```

Mali 设备没有 kgsl 节点，Mesa 直接失败。**必须替换**：

```ini
# 渲染: vkms + swrast 软件光栅化
MESA_LOADER_DRIVER_OVERRIDE=swrast
LIBGL_ALWAYS_SOFTWARE=1

# anland
ANLAND=1
ANLAND_SOCKET=/run/display.sock
ANLAND_DRM_DEVICE=/dev/dri/renderD128
ANLAND_PRESENT_BACKEND=legacy
ANLAND_SKIP_IMPLICIT_SYNC_WAIT=1
XWAYLAND_GBM_DEVICE=/dev/dri/renderD128

# wayland
XDG_SESSION_CLASS=user
XDG_SESSION_TYPE=wayland
XDG_CURRENT_DESKTOP=Hyprland
XDG_SESSION_DESKTOP=Hyprland
QT_QPA_PLATFORM=wayland
```

**`MESA_LOADER_DRIVER_OVERRIDE=swrast` 是必须的。**
只设 `LIBGL_ALWAYS_SOFTWARE=1` 时 Mesa 会尝试 Zink 并失败：

```
MESA: error: ZINK: vkEnumeratePhysicalDevices failed
MESA-EGL: warning: egl: failed to create dri2 screen
```

实测对比（Wayland 平台 EGL）：

| 配置 | 结果 |
|------|:---:|
| 默认 | ❌ eglInitialize failed |
| `GALLIUM_DRIVER=llvmpipe` | ❌ eglInitialize failed |
| `MESA_LOADER_DRIVER_OVERRIDE=zink` | ❌ eglInitialize failed |
| **`MESA_LOADER_DRIVER_OVERRIDE=swrast`** | ✅ **EGL 1.5** |
| `MESA_LOADER_DRIVER_OVERRIDE=kms_swrast` | ✅ EGL 1.5 |
| `EGL_PLATFORM=gbm` | ✅ EGL 1.5 |

### 3.3 创建容器

```bash
su -c '/data/local/Droidspaces/bin/droidspaces --name=anland \
    --rootfs=/data/local/Droidspaces/rootfs-ubuntu \
    --anland --gpu --net=host \
    --init=/usr/lib/systemd/systemd start'
```

成功标志：

```
[Anland] display socket bind-mounted to /run/display.sock
[GPU] Mirrored node: ./dev/dri/renderD128  (226:128)
Container 'anland' is running in background.
```

**验证 socket 一致性**（必须 inode 相同）：

```bash
S=$(ls /data/local/Droidspaces/Net/anland-*.sock | head -1)
stat -c '%F inode=%i' "$S"
stat -c '%F inode=%i' /data/local/Droidspaces/rootfs-ubuntu/run/display.sock
# 两者必须都是 socket 且 inode 相同
```

如果容器内是 `regular empty file`，说明 bind mount 失败——**必须干净重建容器**，
手动 `mount --bind` 会把 socket 变成普通文件。

### 3.4 启动 Hyprland

```bash
su -c '/data/local/Droidspaces/bin/droidspaces --name=anland run -- /bin/sh /root/hnx.sh'
```

脚本内容（`/root/hnx.sh`）：

```sh
#!/bin/sh
export XDG_RUNTIME_DIR=/run/user/1000
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
export ANLAND=1
export ANLAND_SOCKET=/run/display.sock
export ANLAND_DRM_DEVICE=/dev/dri/renderD128
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=Hyprland
export MESA_LOADER_DRIVER_OVERRIDE=swrast
export LIBGL_ALWAYS_SOFTWARE=1

pkill -9 -f Hyprland 2>/dev/null
pkill -9 -f Xwayland 2>/dev/null
sleep 2
rm -rf "$XDG_RUNTIME_DIR"/wayland-* "$XDG_RUNTIME_DIR"/hypr 2>/dev/null

nohup /usr/bin/Hyprland --i-am-really-stupid > /root/hx.log 2>&1 &
sleep 12
pgrep -a Hyprland | head -2
```

**注意 `--i-am-really-stupid`**：Hyprland 默认拒绝以 root 运行。

**必须关掉 Xwayland**（`/root/.config/hypr/hyprland.conf`）：

```ini
xwayland {
    enabled = false
}
```

否则 Xwayland 报 `(EE) failed to read Wayland events: Broken pipe` 后带崩合成器。

---

## 第四步：连接 app（关键：anlandproxy）

### 为什么需要代理

anland daemon 的校验逻辑：

```c
// src/android/anland/display_daemon.c
if (hdr.type == CTRL_MSG_CONSUMER_HELLO)
    allowed = peer_is_root(fd);        // consumer 必须 uid == 0
else if (hdr.type == CTRL_MSG_PRODUCER_HELLO)
    allowed = peer_in_container(fd);   // producer 必须在不同 PID namespace
```

而 **KernelSU 从普通 app 的 mount namespace 完全隐藏了 `su` 二进制**：

```
app 的 namespace: mnt:[4026537016]
/system/bin 有 433 个文件，但 su 不在！
```

app 的 `use_root=true` 探测因此失败：

```
W Anland: root socket probe failed:
          java.io.IOException: Cannot run program "su": error=2, No such file or directory
```

### 代理方案

`anlandproxy` 以 root 运行，监听 app 能访问的路径，转发到真 daemon。
**关键：必须原样转发 SCM_RIGHTS 附带 dmabuf fd。**

```bash
# 编译（NDK，静态）
$NDK/toolchains/llvm/prebuilt/*/bin/aarch64-linux-android26-clang \
    -O2 -static -o anlandproxy anlandproxy.c

# 部署
adb push anlandproxy /data/local/tmp/
adb shell su -c 'chmod 755 /data/local/tmp/anlandproxy'

# 启动
su -c 'S=$(ls /data/local/Droidspaces/Net/anland-*.sock | head -1)
       nohup /data/local/tmp/anlandproxy /data/local/tmp/anland_bridge.sock "$S" \
             > /data/local/tmp/anlandproxy.log 2>&1 &'
```

`anlandproxy.c` 源码见 `C:\nomount\wsl\anlandproxy.c`。

### 配置 app

```bash
su -c 'cat > /data/data/com.anland.consumer/shared_prefs/anland_settings.xml <<EOF
<?xml version="1.0" encoding="utf-8" standalone="yes" ?>
<map>
    <int name="topapp_mode" value="1" />
    <boolean name="use_root" value="false" />
    <string name="socket_path">/data/local/tmp/anland_bridge.sock</string>
</map>
EOF
chown u0_a437:u0_a437 /data/data/com.anland.consumer/shared_prefs/anland_settings.xml'
```

`use_root=false` + 自定义 `socket_path` 指向代理。

### 成功标志

daemon 日志（`/data/local/Droidspaces/Logs/anland/anland`）：

```
daemon: listening on /data/local/Droidspaces/Net/anland-<hash>.sock
daemon: producer connected
daemon: consumer connected
daemon: consumer deposited 5 fds
daemon: fds delivered to producer        ← 握手完成
daemon: screen info 1264x2517 fmt=1
```

Hyprland 侧：

```
Monitor ANLAND-1 (ID 0): 1264x2517@60.00000 at 0x0
	description: Anland Consumer
```

---

## 第五步：启动应用

**用 shm 渲染的应用，不要用 EGL。**

`alacritty` / `kitty` 通过 EGL 创建 surface，会因 `failed to create dri2 screen`
失败。改用 `foot`（纯 shm）：

```bash
su -c 'apt-get install -y foot'

# 启动（继承 Hyprland 环境）
su -c '/data/local/Droidspaces/bin/droidspaces --name=anland run -- /bin/sh -c "
  export XDG_RUNTIME_DIR=/run/user/1000
  export HYPRLAND_INSTANCE_SIGNATURE=$(ls /run/user/1000/hypr/ | head -1)
  hyprctl dispatch exec foot
"'
```

验证：

```bash
hyprctl clients    # 应显示 foot 窗口 mapped:1 visible:1 monitor:0
```

---

## 可选：MobileGL GPU 加速

把容器内的 OpenGL 转译到 Android 侧的 Mali GPU。

### 编译 Android 版（Mali 渲染服务）

```bash
cd MobileGL
git submodule update --init --depth=1 --recursive
(cd 3rdparty/glslang && python update_glslang_sources.py)

cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release \
  -DMOBILEGL_BUILD_DISAGGREGATED=ON -DMOBILEGL_BUILD_DISAGGREGATED_INPROC=ON \
  -DMOBILEGL_BUILD_TEST=OFF -DMOBILEGL_BUILD_BENCHMARK=OFF
cmake --build build-android --target MobileGL MobileGLServer -j 8

# strip（341MB → 18MB）
llvm-strip --strip-unneeded libMobileGL.so
```

### 启动 server（TCP 模式）

```bash
su -c 'cd /data/local/tmp && setsid env -u MOBILEGL_TRANSPORT \
  -u MOBILEGL_IPC_SERVER_PATH -u MOBILEGL_IPC_RING_MB -u MOBILEGL_IPC_STAGE_MB \
  MOBILEGL_IPC_DIAL=no \
  MOBILEGL_IPC_TOKEN=anland-mobilegl-token-2026 \
  MOBILEGL_BACKEND_TYPE=DirectGLES \
  LD_LIBRARY_PATH=/data/local/tmp \
  ./libMobileGLServer.so tcp://0.0.0.0:40613 --serve \
  > /data/local/tmp/mgl_server.log 2>&1 &'
```

验证监听：

```bash
netstat -tlnp | grep 40613
# tcp 0 0.0.0.0:40613 LISTEN <pid>/libMobileGLServer.so
```

### 编译容器内 glibc 客户端

**在容器内原生编译**（容器本身就是 aarch64 glibc，天然匹配）：

```bash
su -c '/data/local/Droidspaces/bin/droidspaces --name=anland run -- /bin/sh -c "
  apt-get install -y build-essential cmake ninja-build \
      libgbm-dev libegl-dev libgles-dev libvulkan-dev mesa-common-dev
  cd /opt/mobilegl
  cmake -S . -B build-client -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DMOBILEGL_BUILD_DISAGGREGATED=ON \
      -DMOBILEGL_BUILD_TEST=OFF -DMOBILEGL_BUILD_BENCHMARK=OFF
  cmake --build build-client --target MobileGL -j 6
"'
```

产物：`/opt/mobilegl/build-client/libMobileGL.so`（22 MB）+
`libEGL.so` / `libGL.so` 软链接。

### 验证

```bash
export MOBILEGL_TRANSPORT=spawn
export MOBILEGL_IPC_CONTROL=tcp://127.0.0.1:40613
export MOBILEGL_IPC_TOKEN=anland-mobilegl-token-2026
export __EGL_VENDOR_LIBRARY_FILENAMES=/opt/mobilegl/build-client/libEGL.so
export LD_LIBRARY_PATH=/opt/mobilegl/build-client

glmark2-es2-wayland --size=640x480 -b :duration=3
```

期望输出：

```
GL_RENDERER: Espryt (MobileGL Core) (Mali-G720-Immortalis MC12, OpenGL ES 3.2)
GL_VERSION:  4.6.0 MobileGL 26.11-dev, Direct (GLES) Backend
```

### 编译 MobileGL 的坑

| 问题 | 解法 |
|------|------|
| 子模块为空 | `git submodule update --init --recursive` |
| `SPIR-V tools not found` | `cd 3rdparty/glslang && python update_glslang_sources.py` |
| `MG_Gbm not existing directory` | 打包源码时别漏 `MobileGL/MG_Gbm` |
| `MobileGL/Init.cpp` 缺失 | 别漏 `MobileGL/*.cpp` 根目录文件 |
| `glvnd/libeglabi.h` 缺失 | 别漏 `3rdparty/glvnd` |
| `mobilegl_gbm.so` 未生成 | 需 `gbm_backend_abi.h`（不在标准 libgbm-dev 里），可忽略 |

---

## 故障排查

### 容器一启动就退出

```bash
# 看 monitor 日志
tail -20 /data/local/Droidspaces/Logs/<name>/log
```

如果看到 `Monitor performing cleanup` 紧跟 `Container is running in background`，
说明 systemd 退出了。检查 `/etc/environment` 的 kgsl 配置。

### socket 在容器内是普通文件

```bash
stat -c '%F %i' rootfs/run/display.sock     # 期望 socket
```

不是 socket 就**干净重建容器**，手动 bind mount 无效。

### Hyprland 崩溃

```bash
grep -aE 'CRIT|ASSERTION|Couldn' /root/hx.log
```

常见原因：
- `Couldn't open a gbm device` → 用 vkms 而非 vgem，并加 `DRIVER_RENDER`
- `ZINK: vkEnumeratePhysicalDevices failed` → 设 `MESA_LOADER_DRIVER_OVERRIDE=swrast`
- Lua 配置语法错误 → 删掉 `/root/.config/hypr/hyprland.lua`

### app 一直跳设置页

```bash
logcat -d -s Anland:*
```

`root socket probe failed: Cannot run program "su"` → 用 anlandproxy 方案。

### EGL 初始化失败

```
eglInitialize failed with error 0x3001
```

Wayland 平台 EGL 需要 `MESA_LOADER_DRIVER_OVERRIDE=swrast`，
只设 `LIBGL_ALWAYS_SOFTWARE=1` 不够。

---

## 已知限制

| 项 | 状态 | 说明 |
|---|---|---|
| CPU limit (`--cpus`) | ❌ | 需 `CONFIG_CFS_BANDWIDTH`，改变调度器结构体 |
| Process limit (`--pids-limit`) | ❌ | 需 `CONFIG_CGROUP_PIDS`，同上 |
| Sandboxing (`--allow-sandboxing`) | ❌ | 需 `CONFIG_USER_NS` 核心路径 |
| GPU 加速收益 | ⚠️ | MobileGL 通路可用，但瓶颈在 anland 合成 |
| 音频 | 未验证 | anland 有音频通道，未测试 |
| 输入法 | 未验证 | fcitx5 已配置，未测试中文输入 |

---

## 关键设备信息

```
设备:       OPPO Find X7 (PHZ110)
SoC:        MT6989
GPU:        Mali-G720-Immortalis MC12
内核:       6.1.157-android14-11-o-g2d576b643492
KMI:        android14-6.1
bootloader: 锁定 (verifiedbootstate=green, flash.locked=1)
Root:       KernelSU 4.3.0-r2
```

---

## 参考

- [anland](https://github.com/SuperTurtleDev/anland) — 显示协议
- [Droidspaces](https://github.com/ravindu644/Droidspaces-OSS) — 容器
- [Droidspaces 官方 anland 文档](https://www.droidspaces.org/docs/anland.html)
- [Desktop-builder](https://github.com/Goldzxcbug/Droidspaces-rootfs-Desktop-builder) — rootfs
- [MobileGL](https://github.com/MobileGL-Dev/MobileGL) — OpenGL 转译
- [droidspaces-lkm](https://github.com/misaka5202/droidspaces-lkm) — LKM 模块（本项目）
