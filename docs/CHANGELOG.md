# Changelog

## v1.0.1

Security audit of the C code. Three findings, all fixed. Nothing here changes
how the modules are used; they close holes that only mattered once a container
was running untrusted workloads.

**Privilege escalation**

- `mount_capable` no longer substitutes `ns_capable()` for filesystems that do
  not carry `FS_USERNS_MOUNT`. The real check has two arms: a global
  `capable(CAP_SYS_ADMIN)` for filesystems the kernel keeps out of user
  namespaces, and `ns_capable(fc->user_ns)` for the ones it allows. Only the
  second is the `CONFIG_USER_NS=n` miscomputation worth correcting, but
  `fc->user_ns` is pinned to init in that configuration, so the old condition
  could not tell them apart. A container root could mount ext4 and anything else
  behind a block device. The fix tests `FS_USERNS_MOUNT` explicitly.

**Panic**

- `droid_lkm_cpu` and `droid_lkm_pids` now hold a reference on each cgroup they
  track, via `percpu_ref_get(&cgrp->self.refcnt)` -- what `cgroup_get()`
  compiles to. Both previously keyed an xarray on a raw `struct cgroup *` and
  relied on `copy_from_kernel_nofault` to spot a freed one, which catches
  unmapped memory but not a slab-reused cgroup whose `kn` field reads back
  non-NULL. That is how the earlier `css_task_iter_advance_css_set` NULL
  dereference happened. Readers now run under `rcu_read_lock`; the destroy path
  erases, drops the reference, calls `synchronize_rcu()` and only then frees.
- The `cgroup_rmdir` kprobe now only sets a dead flag. It used to erase and free
  inline, which raced the tick, and the pids module did not free at all -- every
  entry leaked, and a reused cgroup address let a new container inherit the old
  one's `pids.max`.
- `devtmpfs_lkm`: the trampoline relocation summed `hk_relo_len[]` over the
  instructions ahead of an operand target and indexed the table with
  `hk_insn_index()`, which returns -1 for an unrecognised class. Forward-
  referencing operands could reach an instruction not yet classified, read the
  word before the array, and write a branch to a wrong address into kernel text.
  The lookup now reports failure and the hook aborts with `-EOPNOTSUPP`. The
  staging buffer bound is pinned with a compile-time assertion so a larger
  window cannot silently overflow it.

**Also**

- Build warning-free at `W=1`; a write-only local in the CPU tick loop is gone.

## v1.0

First stable release. Every check `droidspaces check` performs now passes on a
stock 6.1.157 vendor kernel.

**Container capabilities**

- `lkm4ctr` supplies PID, UTS, IPC and user namespaces, plus SYSVIPC and
  POSIX message queues. NET and cgroup namespaces are left to the kernel when it
  already supports them.
- `devtmpfs_lkm` registers a devtmpfs filesystem for `--hw-access` containers.
- `macvlan` gives a container its own interface and MAC on the host NIC.
- `ip6table_nat` provides the IPv6 `nat` table so droidspaces can install its
  NAT66 rule.
- `vkms` provides a render node for Wayland compositors.
- `droid_lkm_pids` emulates the pids controller for `--pids-limit`.
- `droid_lkm_cpu` emulates `cpu.max` for `--cpus`.

**Fixes since the pre-1.0 builds**

- `/proc/<pid>/ns/user` is published, with `vns_userns_operations` installed on
  the init namespace first so `nsfs_evict()` cannot dereference a NULL ops.
- Mount namespaces created from a child user namespace now belong to that
  namespace instead of init, so mounts stop failing with EPERM.
- `mount_capable()` is corrected for the same reason, which is what made tmpfs
  and proc mounts work (`bind` worked earlier because it skips that check).
- NET and cgroup namespaces are no longer stubbed out on kernels that support
  them natively. `--net=macvlan` containers work as a result.
- The pids module no longer panics when a container's cgroup is removed while
  it is still being sampled.
- The `sched_cfs_bandwidth_slice_us` sysctl is registered so the CPU limit
  check reports support.

**Known limits**

- `--net=nat` containers share the host network namespace on kernels without
  native `NET_NS`. Host networking is required in that case.
- IPv6 NAT: the `nat` table and MASQUERADE target are verified; actual packet
  translation was not demonstrable on the test device.
- The `--pids-limit` counter does not include processes migrated into the
  container's cgroup.
- CPU limiting freezes with SIGSTOP rather than CFS throttling, so it is
  coarser than a kernel with `CONFIG_CFS_BANDWIDTH`.