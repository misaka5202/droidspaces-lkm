# Droidspaces LKM

**在原始 GKI 内核上补齐容器能力，无需重编内核、无需刷 boot。**

[English](../README.md) · [Releases](../../releases/latest) · [KMI 覆盖矩阵](KMI-COVERAGE.md)

---

Droidspaces 需要 PID/IPC namespace、devtmpfs、macvlan、IPv6 NAT 以及若干
cgroup 控制器。多数厂商内核为了通过 GKI ABI 检查把这些都编译掉了。本项目
把它们做成可加载模块（`.ko`），在 bootloader 锁定的设备上 `insmod` 即可用。

## 它修好什么

未打补丁的厂商内核上，`droidspaces check` 是这样的：

```
[MUST HAVE]
  [✗] PID namespace
  [✗] IPC namespace
[RECOMMENDED]
  [✗] devtmpfs support
[OPTIONAL]
  [✗] Macvlan support
  [✗] IPv6 NAT support
```

装上本项目后：

```
[MUST HAVE]
  [✓] PID namespace       ← lkm4ctr
  [✓] IPC namespace       ← lkm4ctr
[RECOMMENDED]
  [✓] devtmpfs support    ← devtmpfs_lkm
[OPTIONAL]
  [✓] Macvlan support     ← macvlan
  [✓] IPv6 NAT support    ← ip6table_nat
  [✓] CPU limit support   ← droid_lkm_cpu
  [✓] Process limit       ← droid_lkm_pids

[✓] All required features found!
```

## 包含的模块

| 模块 | 作用 | 补齐的 CONFIG | 开机自启 |
|------|------|---------------|:---:|
| `lkm4ctr.ko` | 命名空间核心 | `PID_NS` / `UTS_NS` / `IPC_NS` / `SYSVIPC` / `POSIX_MQUEUE` | ✅ |
| `devtmpfs_lkm.ko` | 注册 devtmpfs 文件系统 | `DEVTMPFS` | ✅ |
| `macvlan.ko` | 容器独立局域网 IP | `MACVLAN` | ✅ |
| `ip6table_nat.ko` | IPv6 NAT | `IP6_NF_NAT` / `IP6_NF_TARGET_MASQUERADE` | ✅ |
| `vkms.ko` | 虚拟 DRM 设备，提供 render node | `DRM_VKMS`（含 `DRIVER_RENDER` 补丁） | ✅ |
| `droid_lkm_pids.ko` | 仿真 pids 控制器 | `CGROUP_PIDS` | 手动 |
| `droid_lkm_cpu.ko` | 时间片 CPU 配额 | `CFS_BANDWIDTH` | 手动 |

最后两个**只支持手动 `insmod`**，见 [cgroup 限制](#cgroup-限制)。

## 安装

### 方式一：KernelSU / APatch 模块（推荐）

从 [Releases](../../releases/latest) 下载 `droidspaces-lkm-<kmi>-arm64.zip`，
在管理器里刷入，重启。

模块会按 `/proc/sys/kernel/osrelease` 自动选择匹配当前 KMI 的 `.ko`。

### 方式二：手动 insmod

```sh
insmod lkm4ctr-<kmi>.ko
insmod devtmpfs_lkm-<kmi>.ko devtmpfs_probe=2
insmod ip6table_nat-<kmi>.ko
insmod macvlan-<kmi>.ko
insmod vkms-<kmi>.ko                       # 只在需要 render node 时
```

**`devtmpfs_probe=2` 很重要。** 它只注册文件系统，不启动 kthread、不装设备
hook。Droidspaces 只看 `/proc/filesystems` 里有没有 `devtmpfs`；默认路径
（`probe=0`）会装 inline hook，在非目标版本内核上风险更高。

### 验证

```sh
cat /proc/filesystems | grep devtmpfs      # 应出现 devtmpfs
unshare -pf true && echo "PID ns OK"
droidspaces check                          # 应显示 All required features found!
```

## 支持的 KMI

| KMI | 内核 | 构建 |
|-----|------|:---:|
| `android12-5.10` | 5.10 | 自动 |
| `android13-5.10` | 5.10 | 自动 |
| `android13-5.15` | 5.15 | 自动 |
| `android14-5.15` | 5.15 | 自动 |
| `android14-6.1` | 6.1 | 自动 |
| `android15-6.6` | 6.6 | 自动 |
| `android16-6.12` | 6.12 | 自动 |

**仅 arm64。** 非 GKI 内核、非 ARM64 设备不受支持。

每个模块都为每个 KMI 从 DDK 镜像里**该 KMI 自己的内核源码树**构建；
完整矩阵与各 KMI 的构建陷阱见 [KMI-COVERAGE.md](KMI-COVERAGE.md)。

> 同一 KMI 标签的厂商内核 ABI 仍可能有差异。装之前先确认 `uname -r` 里的
> KMI 标签，装完看 `/data/local/tmp/droidspaces-lkm.log`。

## cgroup 限制

`--cpus` 和 `--pids-limit` 是两项无法靠开关配置补上的能力：`CONFIG_CFS_BANDWIDTH=n`
和 `CONFIG_CGROUP_PIDS=n` 时代码路径在编译期就被删掉，没有可"打开"的东西。
这两个模块改为**模拟语义**：

```sh
insmod droid_lkm_pids-<kmi>.ko
insmod droid_lkm_cpu-<kmi>.ko
```

然后按需要带限制启动容器：

```sh
droidspaces --name=box --rootfs=<rootfs> \
    --memory=512M --cpus=0.5 --pids-limit=64 start
```

**`--memory` 是另外两项生效的前提**：仿真的 `pids.max` 与 `cpu.max` 挂在
memory 控制器上，而 Droidspaces 只在配置了内存限制时才启用 memory。

在容器 cgroup 里验证：

```sh
CG=/sys/fs/cgroup/droidspaces/<name>
cat $CG/pids.max          # 64
cat $CG/cpu.max           # 50000
cat $CG/pids.stats        # refused=N, layout=1
```

实现细节、保证与限制：[extra/cgrouplimits/README.md](../extra/cgrouplimits/README.md)。

**请手动加载。** 它们会在运行时解析符号并安装 kprobe；不进开机自启，
意味着任何异常交互都不可能让设备开不了机。

## 未覆盖

| 项 | 原因 |
|---|---|
| `USER_NS`（`--allow-sandboxing`） | 需要整套 userns 核心路径（`map_write`、`create_user_ns` 等）。`CONFIG_USER_NS=n` 时这些代码不在镜像里，模块无法凭空补出来。 |

`--allow-sandboxing` 是唯一剩下的缺口。它属于可选能力，不影响容器启动。

## 重要提醒

### vermagic 不匹配也可能加载成功

内核真正校验的是 `struct module` 的布局（`.gnu.linkonce.this_module` 段大小），
不只是 vermagic 字符串。本项目所有 ko 的 `this_module` 都是 **1088 字节**，
与厂商模块一致，所以 DDK 编出的 ko 在 kernel release 不同的设备上也能加载。

### `devtmpfs_lkm` 千万不要用默认参数开机自启

开机早期加载 `devtmpfs_probe=0`（默认）会**卡开机**：模块会改写内核只读
代码段，而早期启动环境不稳定。**始终传 `devtmpfs_probe=2`。**

### 硬件访问的边界

`--hw-access` 能映射 `/dev/mali0`、`/dev/dma_heap`、`/dev/binder` 等节点，
容器内可以 `open()`。但 Linux 桌面能否真正用上 GPU 取决于驱动栈：

- 高通 Adreno：`/dev/kgsl-3d0` + 用户态 Turnip 可用 —— anland 桌面方案可行，
  且不需要 `vkms`。
- ARM Mali：`/dev/mali0` 是私有 ioctl 接口，Linux Mesa 需要
  `CONFIG_DRM_PANFROST`（内核编译期选项）。此时由 `vkms` 提供 render node。

anland 桌面部署记录：[ANLAND-DEPLOY.md](ANLAND-DEPLOY.md)。

## 构建

CI 在 DDK 容器里构建，其中带各 KMI 真实的 `vmlinux`、`Module.symvers` 和
匹配的 clang：

```bash
make -C /opt/ddk/kdir/<kmi> M=<source-dir> modules
```

vkms、macvlan 这类内核树自带驱动，源码取自**该 KMI 自己的树**而不是共用
副本——它们的 API 会随版本变化。见 [scripts/build_modules.sh](../scripts/build_modules.sh)。

## 源码与许可证

本项目**不重新实现**内核功能，全部为移植与适配：

| 组件 | 来源 | 许可证 |
|------|------|--------|
| `lkm4ctr`（`vendor/` + `glue/`） | [cyanmint/lkm4ctr](https://github.com/cyanmint/lkm4ctr) | 见其 `copying.txt`；`vendor/kernel/` 源自 Linux，**GPL-2.0-only** |
| `devtmpfs_lkm` / `macvlan` / `ip6table_nat` | [ai-luo2006/droidspaces-6.12-pmb110](https://github.com/ai-luo2006/droidspaces-6.12-pmb110) | GPL-2.0 |
| `droid_lkm_pids` | 同项目的 pids Plan B 移植 | GPL-2.0 |
| `droid_lkm_cpu` | 原创 | GPL-2.0 |

**GPL-2.0 要求分发二进制时提供对应源码。** 每个 Release 附带的
`source-<tag>.tar.gz` 就是编出该版本 ko 的完整源码快照。

`vendor/kernel/user_namespace.c`、`vendor/kernel/nsproxy.c`、
`vendor/kernel/pid_namespace.c`、`vendor/ipc/*` 直接来自 Linux
（6.1.124 / android14-6.1），仅做了符号重命名以支持树外构建。

## 参考

- [Droidspaces](https://github.com/ravindu644/Droidspaces-OSS)
- [lkm4ctr](https://github.com/cyanmint/lkm4ctr) — 命名空间模块
- [droidspaces-6.12-pmb110](https://github.com/ai-luo2006/droidspaces-6.12-pmb110) — devtmpfs / macvlan / ip6nat / pids 实现
- [susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) — 发布模式参考

**免责声明**：加载内核模块等同于以最高权限执行内核代码。仅在你能接受设备
无法开机风险的前提下使用，并确保有恢复手段（fastboot / recovery）。
