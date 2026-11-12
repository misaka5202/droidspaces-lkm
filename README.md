# Droidspaces LKM

**在原始 GKI 内核上补齐容器能力，无需重编内核、无需刷 boot。**

Droidspaces 需要 PID/IPC namespace、devtmpfs、macvlan、IPv6 NAT 等内核特性，
而多数厂商内核为了通过 GKI ABI 检查把这些都关掉了。本项目把这些能力做成
可加载模块（`.ko`），`insmod` 即可使用。

---

## 为什么需要它

`droidspaces check` 在未打补丁的厂商内核上是这样的：

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
  [✓] PID namespace       ← lkm4ctr 补齐
  [✓] IPC namespace       ← lkm4ctr 补齐
[RECOMMENDED]
  [✓] devtmpfs support    ← devtmpfs_lkm
[OPTIONAL]
  [✓] Macvlan support     ← macvlan
  [✓] IPv6 NAT support    ← ip6table_nat

[✓] All required features found!
```

实测环境：OPPO Find X7（MT6989 / KMI `android14-6.1`），
容器可正常启动 Alpine / Debian，`pid=1`、`uid=0`、`apk` 可用、硬件节点已映射。

---

## 包含的模块

| 模块 | 作用 | 补齐的 CONFIG |
|------|------|---------------|
| `lkm4ctr.ko` | 命名空间核心 | `PID_NS` / `UTS_NS` / `IPC_NS` / `SYSVIPC` / `POSIX_MQUEUE` |
| `devtmpfs_lkm.ko` | 注册 devtmpfs 文件系统 | `DEVTMPFS` |
| `macvlan.ko` | 容器独立局域网 IP | `MACVLAN` |
| `ip6table_nat.ko` | IPv6 NAT | `IP6_NF_NAT` / `IP6_NF_TARGET_MASQUERADE` |

### 仍未覆盖

| 项 | 原因 |
|---|---|
| `CFS_BANDWIDTH`（`--cpus`） | 改变调度器结构体大小，上千个导出符号 CRC 变化，厂商模块会拒绝加载 |
| `CGROUP_PIDS`（`--pids-limit`） | 同上，改变 cgroup 结构体 |
| `USER_NS`（`--allow-sandboxing`） | 需要内核 `map_write` 等一系列 userns 核心路径，`CONFIG_USER_NS=n` 时这些代码不存在 |

这三项都是 OPTIONAL 分区，不影响容器启动。

---

## 安装

### 方式一：KernelSU 模块（推荐）

从 [Releases](../../releases/latest) 下载 `droidspaces-lkm-<kmi>-arm64.zip`，
在 KernelSU / APatch 管理器里刷入，重启。

模块会按 `/proc/sys/kernel/osrelease` 自动选择匹配当前 KMI 的 `.ko`。

### 方式二：手动 insmod

```sh
# 下载对应 KMI 的 ko
insmod lkm4ctr-<kmi>.ko
insmod devtmpfs_lkm-<kmi>.ko devtmpfs_probe=2
insmod ip6table_nat-<kmi>.ko
insmod macvlan-<kmi>.ko
```

**`devtmpfs_probe=2` 参数很重要**：它表示只注册文件系统、不启动 kthread 和
设备 hook。Droidspaces 只需要 `/proc/filesystems` 里出现 `devtmpfs`，
默认路径（`probe=0`）会安装 inline hook，在版本不匹配的内核上风险更高。

### 验证

```sh
cat /proc/filesystems | grep devtmpfs      # 应有 devtmpfs
unshare -pf true && echo "PID ns OK"       # 应无输出表示成功
droidspaces check                           # 应显示 All required features found
```

---

## 支持的 KMI

| KMI | 内核 | 构建 |
|-----|------|:---:|
| `android12-5.10` | 5.10 | 自动 |
| `android13-5.10` | 5.10 | 自动 |
| `android13-5.15` | 5.15 | 自动 |
| `android14-5.15` | 5.15 | 自动 |
| `android14-6.1` | 6.1 | 自动（已实机验证）|
| `android15-6.6` | 6.6 | 自动 |
| `android16-6.12` | 6.12 | 自动 |

**仅 arm64。** 非 GKI 内核、非 ARM64 设备不受支持。

> 厂商定制内核即使 KMI 相同，ABI 也可能有差异。装之前先确认
> `uname -r` 里的 KMI 标签，装完看 `/data/local/tmp/droidspaces-lkm.log`。

---

## 重要提醒

### `vermagic` 不匹配也可能加载成功

实测：DDK 编出的 ko `vermagic` 是 `6.1.166-dirty`，设备是
`6.1.157-android14-11`，`insmod` 仍然成功。原因是内核真正校验的是
`struct module` 的布局（`.gnu.linkonce.this_module` 段大小），
本项目所有 ko 都是 **1088 字节**，与设备上所有厂商 ko 一致。

### `devtmpfs_lkm` 不要用默认参数开机自启

实测中，`devtmpfs_probe=0`（默认）在开机阶段加载会导致**卡开机**。
模块自身的 hk_patch 会改写内核只读代码段，早期加载时环境不稳定。

**始终使用 `devtmpfs_probe=2`。**

### 硬件访问的边界

`--hw-access` 能映射 `/dev/mali0`、`/dev/dma_heap`、`/dev/binder` 等节点，
实测容器内可 `open()`。但 GPU 能否真正用于 Linux 桌面取决于驱动栈：

- 高通 Adreno：`/dev/kgsl-3d0` + Turnip（用户态 Mesa）可用 → anland 桌面可行
- 联发科 Mali：`/dev/mali0` 是 ARM 私有 ioctl，Linux Mesa 需要
  `CONFIG_DRM_PANFROST`（内核编译期选项）→ **标准桌面方案不可用**

---

## 构建

CI 用 SukiSU-Ultra 的 DDK 容器，其中带该 KMI 真实的 `vmlinux` /
`Module.symvers` / 匹配的 clang：

```bash
make -C /opt/ddk/kdir/<kmi> M=<source-dir> modules
```

本地构建需要对应内核树，见 `docs/BUILD.md`。

---

## 源码与许可证

本项目**不重新实现**任何内核功能，全部为移植与适配：

| 组件 | 来源 | 许可证 |
|------|------|--------|
| `lkm4ctr`（`vendor/` + `glue/`） | [cyanmint/lkm4ctr](https://github.com/cyanmint/lkm4ctr) | 见其 `copying.txt`；其中 `vendor/kernel/` 源自 Linux 内核，**GPL-2.0-only** |
| `devtmpfs_lkm` / `macvlan` / `ip6table_nat` | [ai-luo2006/droidspaces-6.12-pmb110](https://github.com/ai-luo2006/droidspaces-6.12-pmb110) | GPL-2.0 |

**GPL-2.0 要求分发二进制时提供对应源码。** 每个 Release 附带的
`source-<tag>.tar.gz` 就是编出该版本 ko 的完整源码快照。

其中 `vendor/kernel/user_namespace.c`、`vendor/kernel/nsproxy.c`、
`vendor/kernel/pid_namespace.c`、`vendor/ipc/*` 等直接来自 Linux 内核
（6.1.124 / android14-6.1 分支），仅做了符号重命名以支持外部模块构建。

---

## 参考

- [Droidspaces](https://github.com/ravindu644/Droidspaces-OSS)
- [lkm4ctr](https://github.com/cyanmint/lkm4ctr) — 命名空间模块
- [droidspaces-6.12-pmb110](https://github.com/ai-luo2006/droidspaces-6.12-pmb110) — devtmpfs / macvlan / ip6nat 实现
- [susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) — 本项目发布模式参考

**免责声明**：加载内核模块等同于以最高权限执行内核代码。仅在你能接受设备
无法开机风险的前提下使用，并确保有恢复手段（fastboot / recovery）。
