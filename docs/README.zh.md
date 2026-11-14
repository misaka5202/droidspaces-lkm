# Droidspaces LKM

**在原始 GKI 内核上补齐容器能力，无需重编内核、无需刷 boot。**

[English](../README.md) · [Releases](../../releases/latest) · [KMI 覆盖矩阵](KMI-COVERAGE.md)

---

Droidspaces 需要 PID/IPC namespace、devtmpfs、macvlan、IPv6 NAT 以及若干
cgroup 控制器。多数厂商内核为了通过 GKI ABI 检查把这些都编译掉了。

想让它们回来，得自己重编内核再刷进去。这条路能走，就是代价
不小：每个机型要单独编一份，编出来的稳定性得自己担，系统一更新还得重来。
做成模块就避开了这些——一份模块覆盖 7 个 KMI，按 `uname -r` 匹配，
跟机型本身关系不大。

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

装上本项目后，每一行都是勾：

```
[MUST HAVE]
  [✓] Root privileges          [✓] PID namespace
  [✓] Linux version            [✓] Mount namespace
  [✓] UTS namespace            [✓] IPC namespace
  [✓] pivot_root syscall       [✓] /proc filesystem
  [✓] /sys filesystem          [✓] Seccomp support

[RECOMMENDED]
  [✓] epoll / signalfd / PTY / devpts      [✓] Loop device
  [✓] ext4 filesystem                      [✓] Cgroup v2 support
  [✓] Cgroup namespace                     [✓] New mount API
  [✓] devtmpfs support

[OPTIONAL]
  [✓] FUSE support             [✓] TUN/TAP support
  [✓] OverlayFS support        [✓] Network namespace
  [✓] Bridge device support    [✓] Veth pair support
  [✓] Macvlan support          [✓] IPv6 NAT support
  [✓] Memory limit support     [✓] Swap limit support
  [✓] CPU limit support        [✓] CPU usage accounting
  [✓] Process limit support    [✓] Sandboxing (user namespaces)

[✓] All required features found!
```

这是 6.1.157 厂商内核上的真实输出，不是推测。`OPTIONAL` 那一块才是重点：
`CPU limit`、`Process limit`、`Sandboxing` 正是"模块补不了"的三项，也是
本项目以 v1.0.0 而不是 v0.x 发布的原因。

## 包含的模块

| 模块 | 作用 | 补齐的 CONFIG | 开机自启 |
|------|------|---------------|:---:|
| `lkm4ctr.ko` | 命名空间核心 | `PID_NS` / `UTS_NS` / `IPC_NS` / `SYSVIPC` / `POSIX_MQUEUE` | ✅ |
| `devtmpfs_lkm.ko` | 注册 devtmpfs 文件系统 | `DEVTMPFS` | ✅ |
| `macvlan.ko` | 容器独立局域网 IP | `MACVLAN` | ✅ |
| `ip6table_nat.ko` | IPv6 NAT | `IP6_NF_NAT` / `IP6_NF_TARGET_MASQUERADE` | ✅ |
| `vkms.ko` | 虚拟 DRM 设备，提供 render node | `DRM_VKMS`（含 `DRIVER_RENDER` 补丁） | ✅ |
| `droid_lkm_pids.ko` | 仿真 pids 控制器 | `CGROUP_PIDS` | ✅ |
| `droid_lkm_cpu.ko` | 时间片 CPU 配额 | `CFS_BANDWIDTH` | ✅ |

**七个模块全部开机加载。** 两个仿真模块排最后，不会挡住上面五个：它们
失败时设备照常开机、照常跑容器，只是少了对应的限额。想"装但不加载"某个，
在模块目录放 `disable_pids` / `disable_cpu` 文件即可。

## 安装

### 方式一：KernelSU / APatch 模块（推荐）

从 [Releases](../../releases/latest) 下载 `droidspaces-lkm-arm64.zip`，
在管理器里刷入，重启。

包内带 `updateJson`，后续版本管理器会直接提示更新，不用再回这里下载。
如果所在网络访问不了 `raw.githubusercontent.com`，把 `updateJson` 换成 jsDelivr 镜像：

```
updateJson=https://cdn.jsdelivr.net/gh/misaka5202/droidspaces-lkm@master/update.json
```

模块会按 `/proc/sys/kernel/osrelease` 自动选择匹配当前 KMI 的 `.ko`。

### 方式二：手动 insmod

```sh
insmod lkm4ctr-<kmi>.ko
insmod devtmpfs_lkm-<kmi>.ko devtmpfs_probe=2
insmod ip6table_nat-<kmi>.ko
insmod macvlan-<kmi>.ko
insmod vkms-<kmi>.ko
insmod droid_lkm_pids-<kmi>.ko
insmod droid_lkm_cpu-<kmi>.ko
```

`vkms` 只在需要给 Wayland 合成器提供 render node 时才有意义，其他模块不依赖它。

**`devtmpfs_probe=2` 很重要。** 它只注册文件系统，不启动 kthread、不装设备
hook。Droidspaces 只看 `/proc/filesystems` 里有没有 `devtmpfs`；默认路径
（`probe=0`）会装 inline hook，在非目标版本内核上风险更高。

### 验证

```sh
cat /proc/filesystems | grep devtmpfs      # 应出现 devtmpfs
unshare -pf true && echo "PID ns OK"
droidspaces check                          # 应显示 All required features found!
```

## 支持的内核

| 文件 | Android / 内核 |
|---|---|
| `droidspaces-lkm-android12-5.10-arm64.zip` | Android 12 / 5.10 |
| `droidspaces-lkm-android13-5.10-arm64.zip` | Android 13 / 5.10 |
| `droidspaces-lkm-android13-5.15-arm64.zip` | Android 13 / 5.15 |
| `droidspaces-lkm-android14-5.15-arm64.zip` | Android 14 / 5.15 |
| `droidspaces-lkm-android14-6.1-arm64.zip` | Android 14 / 6.1 |
| `droidspaces-lkm-android15-6.6-arm64.zip` | Android 15 / 6.6 |
| `droidspaces-lkm-android16-6.12-arm64.zip` | Android 16 / 6.12 |

`droidspaces-lkm-arm64.zip` 包含全部七种，开机按内核自动选。

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

## 全覆盖

Droidspaces 的每一项检查现在都通过，包括三项看起来模块补不了的：

| 检查 | 做法 |
|------|------|
| `CPU limit support` | `droid_lkm_cpu` 仿真 `cpu.max` + 补注册 `sched_cfs_bandwidth_slice_us` sysctl |
| `Process limit support` | `droid_lkm_pids` 用 kprobe 仿真 pids 控制器 |
| `Sandboxing (user namespaces)` | `lkm4ctr` 通过 `patches/0001-expose-ns-user.patch` 暴露 `/proc/<pid>/ns/user` |

userns 这一项要**三个修复叠加**，少任何一个都只会得到一个"检查全绿但用不了"
的半成品：

1. **暴露 `/proc/<pid>/ns/user`**（`patches/0001`）。发布符号链接只是做了一半：
   `nsfs_evict()` 会无条件解引用 `ns->ns.ops`，而内核静态的 `init_user_ns`
   在 `CONFIG_USER_NS=n` 时没有 ops 初始化器。只加 procfs 条目的补丁会在该
   命名空间第一次被销毁时把内核打崩。所以补丁先照 `kernel/pid_namespace.c`
   处理 `init_pid_ns` 的方式给 init 命名空间装上 `vns_userns_operations`。

2. **修正 mount namespace 的归属**（`patches/0002`）。`CONFIG_USER_NS=n` 时
   `get_user_ns()` 被编成 `return &init_user_ns`，忽略参数。`copy_mnt_ns()`
   用它来钉住新 mount namespace 的属主，于是在子 userns 里创建出来的 mount
   namespace 仍然"属于" init —— `may_mount()` 随即对所有挂载返回 EPERM。

3. **修正 `mount_capable()`**（同样在 `patches/0002`）。同一个 `get_user_ns()`
   bug 让 `alloc_fs_context()` 把 `fc->user_ns` 设成了 init。tmpfs / proc /
   ramfs 都带 `FS_USERNS_MOUNT`，于是走 `ns_capable(fc->user_ns, ...)` 分支
   失败。bind mount 不经过这个检查 —— 这也正是修完第 2 项后 bind 能用、
   tmpfs 还不行原因。

三项都到位后，6.1.157 厂商内核上：`unshare -Ur` 能映射 root，
`uid_map`/`gid_map` 接受 65536 的范围，tmpfs / proc / bind 挂载全部成功 ——
宿主机和 Droidspaces 容器内都一样。

另外两个 namespace 是**相反**的问题：内核本来就支持，是 lkm4ctr 把它们 stub 掉了。

| namespace | 修复前 | 修复后 |
|-----------|--------|--------|
| `NET_NS` | `unshare -n` 静默保持调用方的 ns；`--net=macvlan` 报 "could not create eth0" | 真实 netns，`--net=macvlan` 可用 |
| `CGROUP_NS` | `unshare -C` 保持调用方的 ns | 真实 cgroup namespace |

原实现给 `copy_net_ns` 传 `flags & ~CLONE_NEWNET`，而 `vns_copy_cgroup_ns`
直接忽略 flags。这两处都是**刻意的**：真实 `copy_net_ns` 路径曾导致
percpu_counters 链表损坏。这两个函数开头都是 `inc_ucount(user_ns)`，它会沿
`ns->ucounts` 走并改全局哈希表 —— 而 vendored user_namespace 不参与那套记账，
dmesg 里的 `dec_rlimit_ucounts` 下溢警告正是同一个错配。改传真实
`init_user_ns`（`CONFIG_USER_NS=n` 的内核自己在这些调用点用的就是它），
就把 vendored ns 完全排除在外，原生实现随即正常工作。

CPU limit 那一项也有一个容易被忽略的前提：Droidspaces 要求
`/proc/sys/kernel/sched_cfs_bandwidth_slice_us` 存在，而这个 sysctl 是
`CONFIG_CFS_BANDWIDTH` 保护下注册的，config=n 时内核根本不建。模块用
`register_sysctl()` 真的注册了一张同形状的表（0644、`proc_dointvec_minmax`、
默认 5000），所以那个路径可读可写，不是靠伪造 stat 骗过检查。

注意 `/proc/<pid>/ns/user` 响应的是**按名查找**而不是目录枚举：
`ls /proc/self/ns/` 看不到它，但 `stat` / `access` 会成功。这正是
Droidspaces 实际使用的探测方式。

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
