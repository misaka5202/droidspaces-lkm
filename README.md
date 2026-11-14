# Droidspaces LKM

**Container capabilities for stock GKI kernels — no kernel rebuild, no boot image flashing.**

[![Build](https://github.com/misaka5202/droidspaces-lkm/actions/workflows/build.yml/badge.svg)](https://github.com/misaka5202/droidspaces-lkm/actions/workflows/build.yml)

[中文说明](docs/README.zh.md) · [Releases](../../releases/latest) · [KMI coverage](docs/KMI-COVERAGE.md)

---

Droidspaces needs PID/IPC namespaces, devtmpfs, macvlan, IPv6 NAT and a few
cgroup controllers. Most vendor kernels ship with those compiled out to satisfy
GKI ABI checks.

The other way to get them back is to unlock the bootloader, rebuild the kernel
and flash it. That works, but it costs you: one kernel per device model, the
stability of the result is your problem, and every system update means starting
over. The modules avoid all of that. One set covers seven KMIs, matched against
`uname -r`, so the device model stops mattering.

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

After loading these modules, every line is a tick:

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

That is the real output from a 6.1.157 vendor kernel, not a projection. The
`OPTIONAL` block is the interesting one: `CPU limit`, `Process limit` and
`Sandboxing` are the three a module was supposed to be unable to provide, and
they are the reason this project exists at v1.0.0 rather than v0.x.

## Modules

| Module | Purpose | CONFIG it stands in for | Autoload |
|--------|---------|------------------------|:---:|
| `lkm4ctr.ko` | Namespace core | `PID_NS` / `UTS_NS` / `IPC_NS` / `SYSVIPC` / `POSIX_MQUEUE` | ✅ |
| `devtmpfs_lkm.ko` | Registers a devtmpfs filesystem | `DEVTMPFS` | ✅ |
| `macvlan.ko` | Per-container LAN IP | `MACVLAN` | ✅ |
| `ip6table_nat.ko` | IPv6 NAT | `IP6_NF_NAT` / `IP6_NF_TARGET_MASQUERADE` | ✅ |
| `vkms.ko` | Virtual DRM device, provides a render node | `DRM_VKMS` (with `DRIVER_RENDER` patch) | ✅ |
| `droid_lkm_pids.ko` | Emulated pids controller | `CGROUP_PIDS` | ✅ |
| `droid_lkm_cpu.ko` | Time-slice CPU quota | `CFS_BANDWIDTH` | ✅ |

**All seven load at boot.** The two emulation modules go last and cannot block
the five above them: a failure there leaves a device that still boots and runs
containers, just without the corresponding limit. To install but not load one,
drop a `disable_pids` / `disable_cpu` file in the module directory.

## Install

### Option A — KernelSU / APatch module (recommended)

Download `droidspaces-lkm-arm64.zip` from [Releases](../../releases/latest),
flash it in the manager, reboot.

The module picks the `.ko` matching `/proc/sys/kernel/osrelease` at boot.

The zip carries an `updateJson`, so the manager offers later releases directly;
no need to come back here for a new version. If `raw.githubusercontent.com` is
unreachable on your network, point `updateJson` at the jsDelivr mirror instead:

```
updateJson=https://cdn.jsdelivr.net/gh/misaka5202/droidspaces-lkm@master/update.json
```

### Option B — manual insmod

```sh
insmod lkm4ctr-<kmi>.ko
insmod devtmpfs_lkm-<kmi>.ko devtmpfs_probe=2
insmod ip6table_nat-<kmi>.ko
insmod macvlan-<kmi>.ko
insmod vkms-<kmi>.ko
insmod droid_lkm_pids-<kmi>.ko
insmod droid_lkm_cpu-<kmi>.ko
```

`vkms` is only worth loading if you want a render node for a Wayland
compositor; nothing else depends on it.

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

## Supported kernels

| File | Android / kernel |
|---|---|
| `droidspaces-lkm-android12-5.10-arm64.zip` | Android 12 / 5.10 |
| `droidspaces-lkm-android13-5.10-arm64.zip` | Android 13 / 5.10 |
| `droidspaces-lkm-android13-5.15-arm64.zip` | Android 13 / 5.15 |
| `droidspaces-lkm-android14-5.15-arm64.zip` | Android 14 / 5.15 |
| `droidspaces-lkm-android14-6.1-arm64.zip` | Android 14 / 6.1 |
| `droidspaces-lkm-android15-6.6-arm64.zip` | Android 15 / 6.6 |
| `droidspaces-lkm-android16-6.12-arm64.zip` | Android 16 / 6.12 |

`droidspaces-lkm-arm64.zip` carries all seven and picks the right one at boot.

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

The userns one needs three separate fixes, and each one alone produces a
partially-working namespace that still passes `droidspaces check`:

1. **Publish `/proc/<pid>/ns/user`** (`patches/0001`). Publishing the symlink is
   only half the job: `nsfs_evict()` dereferences `ns->ns.ops` unconditionally,
   and the kernel's static `init_user_ns` has no ops initializer when
   `CONFIG_USER_NS=n`. A patch that only adds the procfs entry crashes the
   kernel the first time that namespace is torn down. The patch installs
   `vns_userns_operations` on the init namespace first, exactly the way
   `kernel/pid_namespace.c` already handles `init_pid_ns`.

2. **Fix mount namespace ownership** (`patches/0002`). Under
   `CONFIG_USER_NS=n`, `get_user_ns()` compiles to `return &init_user_ns`,
   ignoring its argument. `copy_mnt_ns()` uses it to pin the new mount
   namespace's owner, so a namespace created from a child userns still belonged
   to init -- and `may_mount()` then rejected every mount with EPERM.

3. **Fix `mount_capable()`** (also `patches/0002`). The same `get_user_ns()`
   bug makes `alloc_fs_context()` set `fc->user_ns = init`. tmpfs, proc and
   ramfs all carry `FS_USERNS_MOUNT`, so they take the
   `ns_capable(fc->user_ns, ...)` branch and fail. Bind mounts skip that check,
   which is why they started working after fix 2 while tmpfs still did not.

With all three, on a 6.1.157 vendor kernel: `unshare -Ur` maps root,
`uid_map`/`gid_map` accept ranges up to 65536, and tmpfs, proc and bind mounts
all succeed inside the namespace -- on the host and inside a Droidspaces
container.

Two more namespaces had the opposite problem: the kernel supports them natively
and lkm4ctr was stubbing them out.

| Namespace | Before | After |
|-----------|--------|-------|
| `NET_NS` | `unshare -n` silently kept the caller's namespace; `--net=macvlan` failed with "could not create eth0" | real netns, `--net=macvlan` works |
| `CGROUP_NS` | `unshare -C` kept the caller's namespace | real cgroup namespace |

`copy_net_ns` was called with `flags & ~CLONE_NEWNET`, and `vns_copy_cgroup_ns`
ignored its flags entirely. Both were deliberate: the real `copy_net_ns` path
once corrupted the percpu_counters list. Both functions start with
`inc_ucount(user_ns)`, which walks `ns->ucounts` and edits a global hashtable --
accounting a vendored user_namespace does not take part in, which is also what
the `dec_rlimit_ucounts` underflow warnings in dmesg were. Passing the real
`init_user_ns` (what a `CONFIG_USER_NS=n` kernel substitutes at these call
sites) removes the vendored namespace from that code entirely, and the native
implementation then does the right thing.

Note that `/proc/<pid>/ns/user` answers a name lookup, not a directory
listing: `ls /proc/self/ns/` will not show it, but `stat`/`access` succeed.
That is what Droidspaces actually probes.

## Security notes

These modules add kernel capability, so the ways they can go wrong are worth
stating.

- `mount_capable` is corrected only for filesystems carrying `FS_USERNS_MOUNT`.
  Filesystems the kernel deliberately keeps out of user namespaces still require
  global `CAP_SYS_ADMIN`, so a container root cannot mount them.
- The cgroup limit modules hold a reference on every cgroup they track, so a
  cgroup cannot be freed underneath them. Their `cgroup_rmdir` kprobe only marks
  an entry for reaping; the teardown runs on a workqueue where `synchronize_rcu()`
  is legal.
- `devtmpfs_lkm` rewrites `device_add`/`device_del` in kernel text. Its inline
  hook engine refuses a window that crosses a function boundary, refuses an
  instruction it cannot relocate, and clears the BTI guarded attribute only on
  the pages holding resumption points, restoring it on unload. Device nodes it
  creates default to `0600` owned by root when the driver sets no mode.
- Module parameters are all `0444`; nothing is runtime-writable.

See [docs/CHANGELOG.md](docs/CHANGELOG.md) for what each release fixed.

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
