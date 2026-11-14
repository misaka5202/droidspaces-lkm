# Changelog

## v1.0.2

The hook engine no longer runs on kprobe on stock firmware. Same functionality,
different mechanism, plus one privilege escalation that the v1.0.1 audit
introduced.

### The hook backend

`shadow_hook` picks its backend at compile time: ftrace when the kernel has
`CONFIG_FUNCTION_TRACER && CONFIG_DYNAMIC_FTRACE`, otherwise a kprobe
pre_handler that rewrites the trapped pc. Vendor GKI kernels commonly build
FUNCTION_TRACER out -- this device reports `# CONFIG_FUNCTION_TRACER is not
set` -- so kprobe was what actually ran on stock firmware.

kprobe works, but it costs a BRK exception on entry plus a second one from the
kretprobe the engine installs for in-flight tracking, on every hooked call. The
hot hooks (fork, clone, clone3, vfork, unshare, setns, copy_namespaces,
copy_creds, prepare_creds, wake_up_new_task, mount_capable) are all on the
process-creation path. It also occupies the kprobe slot for that address, which
KernelSU/SUSFS and vendor modules share.

An inline hook writes a jump over the entry instead: no exception, no handler,
no shared slot. The displaced instructions are relocated into a trampoline and
that trampoline becomes `hook->original`, so a replacement calling "original"
never lands on the patched prologue -- which removes the recursion guard the
other two backends need.

Two things made this less routine than it sounds:

- **A 20-byte detour cannot patch a syscall wrapper.** The absolute jump is
  `bti jc ; ldr x17,#8 ; br x17 ; .quad target`. `__arm64_sys_msgget` is a
  `bti`, two register moves and a tail `b`, so the branch sits at offset 12 and
  a five-instruction window does not fit. The first attempt let the module
  install 14 hooks and then fail on this one with `-EOPNOTSUPP`, rolling the
  whole init back.
- **A 4-byte `b` cannot reach the general vmalloc area.** With
  `CONFIG_ARM64_VA_BITS=39` the module region ends at `0xffffffc800000000`
  while this kernel's KASLR-randomised text sits around `0xffffffde1b7ceac8` --
  about 95 GB apart, far outside the ±128MB a `b` encodes. That is why the
  kernel itself ships `CONFIG_ARM64_MODULE_PLTS`.

The engine now allocates the trampoline in the vmalloc gap just below the
target (`[addr-112MB, addr-4MB]`) and, when that lands, patches the entry with
a single `b`: 4-byte window, one instruction relocated, no BTI landing pad, no
literal pool. Every hooked function takes that path, syscall wrappers included.
If the near allocation fails the old 20-byte detour is used, and if even that
fails the hook falls back to kprobe -- a tight vmalloc gap degrades instead of
breaking.

### Privilege escalation fixed

- `mount_capable` is now corrected only for filesystems carrying
  `FS_USERNS_MOUNT`. v1.0.1 taught it to substitute `ns_capable(current_userns)`
  whenever the real check failed and `fc->user_ns` was init, but the real
  function has two arms: a global `capable(CAP_SYS_ADMIN)` for filesystems the
  kernel deliberately keeps out of user namespaces, and
  `ns_capable(fc->user_ns)` for the ones it allows. Since `fc->user_ns` is
  pinned to init under `CONFIG_USER_NS=n`, the old condition could not tell
  them apart, and a container root could mount ext4 and anything else behind a
  block device.

### Verified

- All 7 KMIs build.
- On the device before this change: 578 us/fork with the kprobe backend versus
  485 us/fork with the module unloaded, min-of-8. The inline figures for this
  release were not measured -- the handset dropped off USB before the run.

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