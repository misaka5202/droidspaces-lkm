# cgroup limits 仿真模块（--cpus / --pids-limit）

在 `CONFIG_CFS_BANDWIDTH=n`、`CONFIG_CGROUP_PIDS=n` 的原厂内核上，
让 Droidspaces 的 `--cpus` 和 `--pids-limit` **真正生效**。

真机验证：OPPO Find X7 (MT6989, 6.1.157-android14-11, KMI android14-6.1)。

```
droid_lkm_pids.ko   移植自 ai-luo2006/droidspaces-6.12-pmb110 的 Plan B
droid_lkm_cpu.ko    自研时间片限流
```

---

## 为什么需要这两个模块

| 能力 | 内核现状 | 后果 |
|------|---------|------|
| `--pids-limit` | `CONFIG_CGROUP_PIDS=n`，pids 子系统不存在 | 写 `pids.max` 失败，检查项 ✗ |
| `--cpus` | `CONFIG_CFS_BANDWIDTH=n`，`cpu.max` 写路径和执行体（`tg_set_cfs_bandwidth`/`throttle_cfs_rq`）整个被 `#ifdef` 删除 | 写 `cpu.max` 失败，检查项 ✗ |

两个配置的共同点：**代码路径在编译期被删除，LKM 无法"开启"它们**，
只能**模拟**——自己实现文件、记账、执行。

---

## pids 模块（Plan B，移植自 6.12）

四层设计，与上游一致：

| 层 | 手段 |
|----|------|
| 显示层 | kretprobe `cgroup_controllers_show` / `proc_cgroupstats_show`，追加 "pids cpu" |
| 文件层 | `cgroup_add_dfl_cftypes(memory_cgrp_subsys, ...)` 挂 `pids.max`/`pids.current`/`pids.stats` |
| 执行层 | kprobe `cgroup_can_fork` 超限时 `regs->pc = regs->regs[30]` + x0=-EAGAIN（函数体不执行）；`cgroup_post_fork` 记账 +1；`cgroup_exit` -1 |
| 翻译层 | kprobe `cgroup_subtree_control_write`，把 "+pids"/"+cpu" 抹掉、必要时换成 "+memory" |

fork 拒绝的送达方式：kprobe 观察调用，超限时写返回寄存器直接返回，
`fork()` 用户态收到 EAGAIN——与真实控制器行为一致。

关键坑：
- **布局自校验**：`struct css_set` 以 `subsys[CGROUP_SUBSYS_COUNT]` 开头，
  运行时从 `cgroup_subsys[]` 数实际控制器数并修正偏移，双重校验
  （`init_css_set.dfl_cgrp`、`init_task.cgroups`）失败则不装 fork 探针。
- **`+pids` 重复 enable 会 EFAULT**：B3 翻译只在 memory 未启用时 rewrite，
  已启用则清空 buffer 无事返回。
- **显示层必须加 "cpu"**：Droidspaces 读 `cgroup.controllers` 判断 v2 路径，
  只加 "pids" 会让 `--cpus` 走 v1 路径而失败。

### 真机证据

```
$ dmesg | grep droid_lkm_pids
layout verified: controllers=7 (built against 7)
pids limit enforcement active (emulated pids controller)

# 手工实验（pids.max=5）
第1次 current=2 ... 第4次 current=5   ← 涨到上限
第5次 current=2 ... refused=3         ← 被拦截

# Droidspaces 容器（--pids-limit=64）
pids.max = 64
fork 60 个后 refused=2
容器内: /bin/sh: can't fork: Resource temporarily unavailable
```

---

## cpu 模块（自研）

CFS 带宽控制执行体不存在，用**时间片限流**替代：

```
cpu.max 文件   → 挂 memory 子系统，解析 "<quota_us> <period_us>"
记账           → delayed_work 每 10ms 采样：遍历 cgroup 子树进程，
                 task_sched_runtime() 累加 CPU 时间（ns）
窗口判定       → window_used = 当前累计 - 窗口基准；>= quota 即超限
超限动作       → css_task_iter 遍历进程发 SIGSTOP
窗口结束       → 重置基准 + SIGCONT
```

### 走过的五个大坑（每个都导致过 panic 或失效）

1. **hrtimer 上下文 panic**：`filp_open` 走 vfs 路径，在 hrtimer 中断上下文
   调用 → `do_mem_abort` → kernel panic。改 delayed_work（进程上下文）。

2. **CFI 陷阱 panic**：`CONFIG_CFI_CLANG=y` 下，通过 KallRecon 解析的函数
   指针间接调用触发 CFI 陷阱。所有含间接调用的函数标 `__nocfi`。

3. **vfs 读 cpu.stat 被 SELinux 拒**（EACCES）：`filp_open` 打不开 cgroup
   文件。改用内核内部数据。

4. **`struct cgroup.bstat` 偏移不匹配**：设备内核与本地树 commit 不同，
   布局校验通过但 bstat 读到 0。改进程级 `task_sched_runtime` 累加。

5. **采样间隔=窗口周期**：tick 100ms = period 100ms，每次采样要么设基准
   要么窗口重置，永远走不到配额检查。tick 改 10ms。

### 真机证据

```
$ dmesg | grep droid_lkm_cpu
layout ok: controllers=7 bstat_off=984
over quota (62593 >= 50000), SIGSTOP     ← --cpus=0.5 窗口内超限
window reset, SIGCONT
over quota (57774 >= 50000), SIGSTOP
window reset, SIGCONT

容器内 ps 出现 T(stopped) 进程            ← 真的冻结了
```

---

## 已知限制

| 项 | 说明 |
|----|------|
| pids 迁移进程不计 | 计数只统计"在 cgroup 内 fork 的"，迁移进去的那一个不计入（真实控制器差异） |
| cpu 是时间片而非平滑 throttle | SIGSTOP 冻结比 CFS 的 throttle 暴力；窗口粒度内进程可能整段暂停 |
| cpu 记账采样有 10ms 粒度 | busy-loop 场景准确；短突发可能漏计 |
| rmmod 清空配置 | 重载后需重新写 cpu.max/pids.max（Droidspaces 重启容器即可） |
| 冻结只对进程发 SIGSTOP | 容器内进程忽略 SIGSTOP 的场景不存在（STOP 不可捕获） |

---

## 加载方式（insmod，不进开机自启）

```bash
su -c 'insmod /data/local/tmp/droid_lkm_pids.ko'
su -c 'insmod /data/local/tmp/droid_lkm_cpu.ko'

# 卸载（先停容器）
su -c 'rmmod droid_lkm_cpu; rmmod droid_lkm_pids'
```

**重要**：加载顺序无关，但 Droidspaces 创建容器时**必须两个模块都在**，
否则限制写入失败后被跳过（需要重启容器重试）。

---

## 构建

```bash
# WSL，内核树 ~/zram/common（6.1.157）
bash /mnt/c/nomount/wsl/build_pids.sh
bash /mnt/c/nomount/wsl/build_cpu.sh
```

需要：clang20、内核树 with out/ 已配置、`net_ip6nat/kr/` 的 KallRecon
（复制到本目录）。

## 文件

```
extra/cgrouplimits/
├── Makefile
├── pids_mod.c          pids 仿真（6.12 Plan B 移植 + "cpu" 显示）
├── cpu_mod.c           cpu.max 时间片限流
└── kr/                 KallRecon（复制自 net_ip6nat/kr）
```
