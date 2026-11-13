# Droidspaces LKM

**Container capabilities for stock GKI kernels — no kernel rebuild, no boot image flashing.**

[![Build](https://github.com/misaka5202/droidspaces-lkm/actions/workflows/build.yml/badge.svg)](https://github.com/misaka5202/droidspaces-lkm/actions/workflows/build.yml)

[中文说明](docs/README.zh.md) · [Releases](../../releases/latest) · [KMI coverage](docs/KMI-COVERAGE.md)

---

Droidspaces needs PID/IPC namespaces, devtmpfs, macvlan, IPv6 NAT and a few
cgroup controllers. Most vendor kernels ship with those compiled out to satisfy
GKI ABI checks. This project packages them as loadable modules (`.ko`) that you
`insmod` on a locked-bootloader device.

## What it fixes

`droidspaces check` on an unpatched vendor kernel:

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

After loading these modules:

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

## Modules

| Module | Purpose | CONFIG it stands in for | Autoload |
|--------|---------|------------------------|:---:|
| `lkm4ctr.ko` | Namespace core | `PID_NS` / `UTS_NS` / `IPC_NS` / `SYSVIPC` / `POSIX_MQUEUE` | ✅ |
| `devtmpfs_lkm.ko` | Registers a devtmpfs filesystem | `DEVTMPFS` | ✅ |
| `macvlan.ko` | Per-container LAN IP | `MACVLAN` | ✅ |
| `ip6table_nat.ko` | IPv6 NAT | `IP6_NF_NAT` / `IP6_NF_TARGET_MASQUERADE` | ✅ |
| `vkms.ko` | Virtual DRM device, provides a render node | `DRM_VKMS` (with `DRIVER_RENDER` patch) | ✅ |
| `droid_lkm_pids.ko` | Emulated pids controller | `CGROUP_PIDS` | manual |
| `droid_lkm_cpu.ko` | Time-slice CPU quota | `CFS_BANDWIDTH` | manual |

The last two are **manual `insmod` only** — see [cgroup limits](#cgroup-limits).

## Install

### Option A — KernelSU / APatch module (recommended)

Download `droidspaces-lkm-<kmi>-arm64.zip` from
[Releases](../../releases/latest), flash it in the manager, reboot.

The module picks the `.ko` matching `/proc/sys/kernel/osrelease` at boot.

### Option B — manual insmod

```sh
insmod lkm4ctr-<kmi>.ko
insmod devtmpfs_lkm-<kmi>.ko devtmpfs_probe=2
insmod ip6table_nat-<kmi>.ko
insmod macvlan-<kmi>.ko
insmod vkms-<kmi>.ko                       # only if you need a render node
```

**`devtmpfs_probe=2` matters.** It registers the filesystem and starts no
kthread or device hooks. Droidspaces only looks for `devtmpfs` in
`/proc/filesystems`; the default path (`probe=0`) installs inline hooks, which
is riskier on a kernel it was not built against.

### Verify

```sh
cat /proc/filesystems | grep devtmpfs      # devtmpfs should appear
unshare -pf true && echo "PID ns OK"
droidspaces check                          # "All required features found!"
```

## Supported KMI

| KMI | Kernel | Build |
|-----|--------|:---:|
| `android12-5.10` | 5.10 | auto |
| `android13-5.10` | 5.10 | auto |
| `android13-5.15` | 5.15 | auto |
| `android14-5.15` | 5.15 | auto |
| `android14-6.1` | 6.1 | auto |
| `android15-6.6` | 6.6 | auto |
| `android16-6.12` | 6.12 | auto |

**arm64 only.** Non-GKI kernels and non-ARM64 devices are not supported.

Every module is built for every KMI from that KMI's own kernel source tree in
the DDK image; see [docs/KMI-COVERAGE.md](docs/KMI-COVERAGE.md) for the exact
matrix and the per-KMI build traps.

> A vendor kernel with the same KMI tag can still differ in ABI. Check the KMI
> tag in `uname -r` first, and read `/data/local/tmp/droidspaces-lkm.log` after
> installing.

## cgroup limits

`--cpus` and `--pids-limit` are the two knobs that cannot be fixed by toggling a
config: with `CONFIG_CFS_BANDWIDTH=n` and `CONFIG_CGROUP_PIDS=n` the code paths
are deleted at compile time, so there is nothing to enable. These two modules
emulate the semantics instead:

```sh
insmod droid_lkm_pids-<kmi>.ko
insmod droid_lkm_cpu-<kmi>.ko
```

Then start the container with the limits you want:

```sh
droidspaces --name=box --rootfs=<rootfs> \
    --memory=512M --cpus=0.5 --pids-limit=64 start
```

`--memory` is required for the other two to land: the emulated `pids.max` and
`cpu.max` hang off the memory controller, and Droidspaces only enables memory
when a memory limit is configured.

Verify inside the container cgroup:

```sh
CG=/sys/fs/cgroup/droidspaces/<name>
cat $CG/pids.max          # 64
cat $CG/cpu.max           # 50000
cat $CG/pids.stats        # refused=N, layout=1
```

Details, guarantees and limitations: [extra/cgrouplimits/README.md](extra/cgrouplimits/README.md).

**Load them manually.** They install kprobes and resolve symbols at runtime;
keeping them out of boot autostart means a bad interaction can never stop the
device from booting.

## Full coverage

Every check Droidspaces performs now passes, including the three that look
impossible from a module:

| Check | How |
|-------|-----|
| `CPU limit support` | `droid_lkm_cpu` emulates `cpu.max`, and registers the `sched_cfs_bandwidth_slice_us` sysctl the check also wants |
| `Process limit support` | `droid_lkm_pids` emulates the pids controller with kprobes |
| `Sandboxing (user namespaces)` | `lkm4ctr` exposes `/proc/<pid>/ns/user` via `patches/0001-expose-ns-user.patch` |

The CPU one has a second precondition that is easy to miss: Droidspaces also
requires `/proc/sys/kernel/sched_cfs_bandwidth_slice_us` to exist, and that sysctl
is registered from `kernel/sched/fair.c` inside `#ifdef CONFIG_CFS_BANDWIDTH`, so
a kernel with the config off never creates the file. The module registers a real
table with `register_sysctl()` in the same shape fair.c uses (0644,
`proc_dointvec_minmax`, default 5000), so the path is readable and writable
rather than faked.

The userns one is worth a note too, because it is subtle: publishing the symlink is
only half the job. `nsfs_evict()` dereferences `ns->ns.ops` unconditionally,
and the kernel's static `init_user_ns` has no ops initializer when
`CONFIG_USER_NS=n`. A patch that only adds the procfs entry crashes the kernel
the first time that namespace is torn down. The patch therefore also installs
`vns_userns_operations` on the init namespace first, exactly the way
`kernel/pid_namespace.c` already handles `init_pid_ns`, and refuses to publish
the symlink when it cannot.

Note that `/proc/<pid>/ns/user` answers a name lookup, not a directory
listing: `ls /proc/self/ns/` will not show it, but `stat`/`access` succeed.
That is what Droidspaces actually probes.

## Important notes

### A mismatched vermagic can still load

The kernel validates the layout of `struct module` (the size of
`.gnu.linkonce.this_module`), not only the vermagic string. Every module here is
built to the same 1088-byte `this_module` as the vendor modules, which is why a
ko built by the DDK loads on a device whose kernel release differs.

### Never autoload `devtmpfs_lkm` with default parameters

`devtmpfs_probe=0` (the default) during early boot can hang the device: the
module rewrites kernel text and the early-boot environment is not stable.
**Always pass `devtmpfs_probe=2`.**

### Hardware access boundaries

`--hw-access` maps nodes such as `/dev/mali0`, `/dev/dma_heap`, `/dev/binder`
and they can be `open()`ed inside the container. Whether a Linux desktop can
actually use the GPU depends on the driver stack:

- Qualcomm Adreno: `/dev/kgsl-3d0` plus Turnip in userspace works — a Wayland
  desktop over anland is viable, and needs no `vkms`.
- ARM Mali: `/dev/mali0` is a private ioctl interface; Linux Mesa needs
  `CONFIG_DRM_PANFROST`, which is a kernel build option. `vkms` supplies the
  render node instead.

Deployment notes for a Wayland desktop over anland:
[docs/ANLAND-DEPLOY.md](docs/ANLAND-DEPLOY.md).

## Build

CI builds inside the DDK container, which carries the real `vmlinux`,
`Module.symvers` and matching clang for each KMI:

```bash
make -C /opt/ddk/kdir/<kmi> M=<source-dir> modules
```

The per-KMI driver sources (vkms, macvlan) come from that KMI's own tree, not
from a shared copy — their APIs move between releases. See
[scripts/build_modules.sh](scripts/build_modules.sh).

## Sources and licensing

This project does not reimplement kernel functionality; everything is ported or
adapted:

| Component | Origin | License |
|-----------|--------|---------|
| `lkm4ctr` (`vendor/` + `glue/`) | [cyanmint/lkm4ctr](https://github.com/cyanmint/lkm4ctr) | see its `copying.txt`; `vendor/kernel/` comes from Linux, **GPL-2.0-only** |
| `devtmpfs_lkm` / `macvlan` / `ip6table_nat` | [ai-luo2006/droidspaces-6.12-pmb110](https://github.com/ai-luo2006/droidspaces-6.12-pmb110) | GPL-2.0 |
| `droid_lkm_pids` | port of the same project's pids Plan B | GPL-2.0 |
| `droid_lkm_cpu` | original | GPL-2.0 |

**GPL-2.0 requires source for binaries.** Every release carries
`source-<tag>.tar.gz`, the exact tree the `.ko` files were built from.

`vendor/kernel/user_namespace.c`, `vendor/kernel/nsproxy.c`,
`vendor/kernel/pid_namespace.c`, `vendor/ipc/*` come from Linux
(6.1.124 / android14-6.1) with symbol renames so they can be built out of tree.

## References

- [Droidspaces](https://github.com/ravindu644/Droidspaces-OSS)
- [lkm4ctr](https://github.com/cyanmint/lkm4ctr)
- [droidspaces-6.12-pmb110](https://github.com/ai-luo2006/droidspaces-6.12-pmb110)
- [susfs4ksu-lkm](https://github.com/inforcqb/susfs4ksu-lkm) — release pattern reference

**Disclaimer**: loading a kernel module runs kernel code with full privilege.
Use this only if you can accept the risk of an unbootable device, and have a
recovery path (fastboot / recovery).
