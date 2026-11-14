// SPDX-License-Identifier: GPL-2.0-only
/*
 * droid_lkm_cpu — cpu.max 模拟（6.1 自研版）
 *
 * 目标：让 Droidspaces 的 --cpus（CPU 配额）在 CONFIG_CFS_BANDWIDTH=n 的
 * 内核上真正生效。
 *
 * CFS_BANDWIDTH=n 的后果：
 *   - cpu_cfs_quota_us / cpu.max 的写路径不存在（cpu_max_write 整个在
 *     #ifdef 里被删掉）
 *   - throttle_cfs_rq / tg_set_cfs_bandwidth 执行体不存在
 *
 * 本模块用「时间片限流」替代 CFS 的平滑 throttle：
 *   1. cpu.max 文件挂在 memory 子系统上（v2 根只有 memory 可用，
 *      与 droid_lkm_pids 同理）
 *   2. write 解析 "<quota_us> <period_us>"，存进 per-cgroup xarray
 *   3. delayed_work 每 100ms 采样该 cgroup 的 cpu.stat usage_usec
 *   4. 窗口内用量超 quota → 把 cgroup 进程冻结（v1 freezer FROZEN）
 *   5. 窗口结束 → used 重置、解冻
 *
 * 冻结是"硬限流"而非"软限流"：被冻结的窗口进程完全不运行，之后恢复。
 * 这比 CFS 的 throttle 更暴力，但效果是真实的上限，不欺骗。
 *
 * 记账来源：/sys/fs/cgroup/<路径>/cpu.stat 的 usage_usec。
 * 该文件在这台设备上存在（vendor 内核的 cpu 子系统提供），
 * 用 filp_open + kernel_read 读取（每 100ms 一次，开销可忽略）。
 *
 * 加载方式：insmod，不设开机自启。所有探针 best-effort。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/xarray.h>
#include <linux/cgroup.h>
#include <linux/cgroup-defs.h>
#include <linux/kernfs.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/atomic.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/sysctl.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/vmalloc.h>

#include "kr/core.h"

#define TAG "droid_lkm_cpu"
#define cpu_info(fmt, ...)  pr_info("[" TAG "] " fmt, ##__VA_ARGS__)
#define cpu_warn(fmt, ...)  pr_warn("[" TAG "] " fmt, ##__VA_ARGS__)
#define cpu_err(fmt, ...)   pr_err("[" TAG "] " fmt, ##__VA_ARGS__)

/* 采样周期：10ms。必须远小于默认 period(100ms)，否则"设基准"和
 * "窗口重置"两个分支就会占满每次 tick，永远走不到配额检查。 */
#define CPU_TICK_MS 10

/* per-cgroup 状态 */
struct cpu_priv {
	atomic64_t quota_us;	/* 0=未启用; S64_MAX="max" 无限 */
	atomic64_t period_us;
	atomic64_t used_us;	/* 本窗口累计 */
	atomic64_t window_start;/* ktime_to_us */
	atomic64_t window_usage_base; /* 窗口起点累计 usage（us） */
	atomic64_t frozen;	/* 0/1：当前是否被冻结 */
	atomic_t dead;		/* 1=cgroup 已 rmdir，等待 tick 回收 */
};

static DEFINE_XARRAY(cpu_xa);

/*
 * [安全] 条目生命周期。
 *
 * 原来的做法是 xarray 里存裸 struct cgroup *，销毁靠 cgroup_rmdir kprobe
 * 里直接 xa_erase + kfree。这有三个真实缺陷：
 *
 *   1. cgroup 被释放后指针悬垂；copy_from_kernel_nofault 只能挡未映射内存，
 *      挡不住"内存被 slab 复用后读到垃圾但非 NULL"，于是仍会把野指针当
 *      struct cgroup 用 —— 这正是之前 css_task_iter_advance_css_set 空指针
 *      panic 的来源。
 *   2. kprobe 上下文里 xa_erase/kfree，与 tick 遍历并发 → 释放后使用。
 *   3. cpu_find() 返回 p 后没有任何引用计数，tick 里 kfree 与读写方竞争。
 *
 * 现在的做法：
 *   - 建条目时用 percpu_ref_get(&cgrp->self.refcnt) 抓住 cgroup
 *     （等价于内核自己的 cgroup_get()）。cgroup 内存因此在整个条目存活
 *     期间都有效，野指针问题从根上消失。
 *   - 读方用 rcu_read_lock()，释放方先 xa_erase 再 synchronize_rcu()
 *     再 kfree，保证没有读方还握着 p。
 *   - kprobe 只置 dead 标记（不睡眠、不释放），真正回收放到 tick 工作队列。
 *   - 所有增删 cpu_priv 的地方都持 cpu_lock。
 */
static DEFINE_MUTEX(cpu_lock);

/* 等价于内核 cgroup_get()：cgroup_get() 是 static inline，模块里拿不到符号 */
static void ctl_cgroup_get(struct cgroup *cgrp)
{
	if (!(READ_ONCE(cgrp->self.flags) & CSS_NO_REF))
		percpu_ref_get(&cgrp->self.refcnt);
}

static void ctl_cgroup_put(struct cgroup *cgrp)
{
	if (!(READ_ONCE(cgrp->self.flags) & CSS_NO_REF))
		percpu_ref_put(&cgrp->self.refcnt);
}

/* 调用前索引已被摘除；必须在可睡眠上下文调用（synchronize_rcu 会睡） */
static void cpu_free_entry(unsigned long idx, struct cpu_priv *p)
{
	ctl_cgroup_put((struct cgroup *)idx);
	synchronize_rcu();
	kfree(p);
}

/* 读方用：进入 RCU 临界区后才能安全解引用返回的 p */
static struct cpu_priv *cpu_find(struct cgroup *cgrp)
{
	return xa_load(&cpu_xa, (unsigned long)cgrp);
}

/* ---------------------------------------------------------- cpu.max --- */

static u64 cpu_max_read_u64(struct cgroup_subsys_state *css,
			    struct cftype *cft)
{
	struct cgroup *cgrp = css->cgroup;
	struct cpu_priv *p;
	u64 v = 0;

	rcu_read_lock();
	p = cpu_find(cgrp);
	if (p)
		v = (u64)atomic64_read(&p->quota_us);
	rcu_read_unlock();
	return v;
}

static ssize_t cpu_max_write(struct kernfs_open_file *of, char *buf,
			     size_t nbytes, loff_t off)
{
	struct cgroup *cgrp = of->kn->parent->priv;
	char tmp[64];
	size_t i = 0, j = 0;
	s64 quota, period = 100000;
	struct cpu_priv *p;
	char *end;

	/* copy the (not null terminated) write buffer */
	while (i < nbytes && j < sizeof(tmp) - 1 && buf[i] != '\n')
		tmp[j++] = buf[i++];
	tmp[j] = '\0';
	while (j > 0 && (tmp[j - 1] == ' ' || tmp[j - 1] == '\t'))
		tmp[--j] = '\0';

	if (strcmp(tmp, "max") == 0) {
		quota = S64_MAX;
	} else {
		long q = simple_strtol(tmp, &end, 10);

		if (end == tmp)
			return -EINVAL;
		quota = (s64)q;
		/* 可选 " <period>" */
		while (*end == ' ' || *end == '\t')
			end++;
		if (*end != '\0') {
			long pr = simple_strtol(end, &end, 10);

			if (*end != '\0')
				return -EINVAL;
			if (pr > 0)
				period = (s64)pr;
		}
		if (quota < 0)
			quota = 0;
	}

	/*
	 * 整段持 cpu_lock：建条目要取 cgroup 引用，且建/改必须和 tick 的
	 * 回收串行。写 cpu.max 是低频操作，锁竞争可以忽略。
	 */
	mutex_lock(&cpu_lock);
	p = cpu_find(cgrp);
	if (!p) {
		p = kzalloc(sizeof(*p), GFP_KERNEL);
		if (!p) {
			mutex_unlock(&cpu_lock);
			return -ENOMEM;
		}
		atomic64_set(&p->quota_us, 0);
		atomic64_set(&p->period_us, 100000);
		atomic64_set(&p->used_us, 0);
		atomic64_set(&p->window_start, 0);
		atomic64_set(&p->window_usage_base, 0);
		atomic64_set(&p->frozen, 0);
		atomic_set(&p->dead, 0);
		/*
		 * 先抓住 cgroup：条目存活期间它就不会被释放，tick 里拿到的
		 * struct cgroup * 因此永远有效。写 cftype 时调用方持有该
		 * cgroup 的活动引用，此刻取引用是安全的。
		 */
		ctl_cgroup_get(cgrp);
		{
			struct cpu_priv *old =
				xa_store(&cpu_xa, (unsigned long)cgrp, p,
					 GFP_KERNEL);
			if (xa_is_err(old)) {
				int err = xa_err(old);

				ctl_cgroup_put(cgrp);
				kfree(p);
				mutex_unlock(&cpu_lock);
				return err;
			}
			if (old) {
				/* 别人抢先建了：放掉我们多取的那一份 */
				ctl_cgroup_put(cgrp);
				kfree(p);
				p = old;
			}
		}
	}
	atomic64_set(&p->quota_us, quota);
	atomic64_set(&p->period_us, period > 0 ? period : 100000);
	atomic64_set(&p->used_us, 0);
	atomic64_set(&p->window_start, ktime_to_us(ktime_get()));
	atomic64_set(&p->window_usage_base, 0);
	atomic64_set(&p->frozen, 0);
	atomic_set(&p->dead, 0);
	mutex_unlock(&cpu_lock);
	cpu_info("cpu.max=%lld/%lld on cgroup %px\n",
		 (long long)quota, (long long)period, cgrp);
	return (ssize_t)nbytes;
}

static int cpu_max_show(struct seq_file *sf, void *v)
{
	struct cgroup_subsys_state *css = seq_css(sf);
	struct cpu_priv *p;
	u64 quota = 0, period = 100000;

	rcu_read_lock();
	p = cpu_find(css->cgroup);
	if (p) {
		quota = atomic64_read(&p->quota_us);
		period = atomic64_read(&p->period_us);
	}
	rcu_read_unlock();

	if (quota == S64_MAX)
		seq_puts(sf, "max\n");
	else
		seq_printf(sf, "%llu %llu\n", quota, period);
	return 0;
}

static struct cftype cpu_max_files[] = {
	{
		.name = "cpu.max",
		.flags = CFTYPE_NO_PREFIX | CFTYPE_NOT_ON_ROOT,
		.read_u64 = cpu_max_read_u64,
		.write = cpu_max_write,
	},
	{
		.name = "cpu.limit.stats",
		.flags = CFTYPE_NO_PREFIX | CFTYPE_NOT_ON_ROOT,
		.seq_show = cpu_max_show,
	},
	{},
};

/* ---------------------------------------------------------- 冻结动作 ---
 *
 * 用 SIGSTOP/SIGCONT 而不是 v1 freezer：
 *   - 不需要把进程迁到别的 cgroup 层级（v1 freezer 是独立层级）
 *   - SIGSTOP 的进程进入 T 态，cpu.stat 的 usage 不再增长，正是限流效果
 *   - SIGCONT 恢复，进程继续
 * 通过 kallsyms 解析 css_task_iter / send_sig_info（均未导出）。
 */

static void (*p_css_task_iter_start)(struct cgroup_subsys_state *css,
				     unsigned int flags,
				     struct css_task_iter *it);
static struct task_struct *(*p_css_task_iter_next)(struct css_task_iter *it);
static void (*p_css_task_iter_end)(struct css_task_iter *it);
static int (*p_send_sig_info)(int sig, struct kernel_siginfo *info,
			      struct task_struct *p);

static void (*p_rstat_flush_hold)(struct cgroup *cgrp);
static unsigned long long (*p_task_sched_runtime)(struct task_struct *p);
static void (*p_rstat_flush_release)(void);
static unsigned long ctl_bstat_off;
static unsigned long ctl_kn_priv_off;

static bool ctl_read_ptr(unsigned long addr, unsigned long *out)
{
	return copy_from_kernel_nofault(out, (void *)addr, sizeof(*out)) == 0;
}

/*
 * cgroup 指针有效性校验。
 *
 * 没有它就会 panic（实测：`css_task_iter_advance_css_set+0x28` 空指针
 * 解引用，地址 0x318）。原因是 xarray 里存的是裸 cgroup 指针，容器重启后
 * 旧 cgroup 被 rmdir 释放，但条目还在；tick 遍历到它时把已释放内存当成
 * struct cgroup 用。
 *
 * 两道防线：
 *   1. kprobe cgroup_rmdir 在删除时清掉对应条目（主动回收）
 *   2. 遍历前用 copy_from_kernel_nofault 探该 cgroup 的 kn 字段，
 *      读不到就跳过并顺手清掉（防御残留与竞态）
 */
static bool ctl_cgrp_alive(struct cgroup *cgrp)
{
	unsigned long kn = 0;

	if (!cgrp)
		return false;
	/* cgrp->kn 是第一个指针字段之后的常用字段，用 nofault 读避免
	 * 触碰已释放内存时再次崩在这里 */
	if (!ctl_read_ptr((unsigned long)cgrp + offsetof(struct cgroup, kn),
			  &kn))
		return false;
	/* 活着的 cgroup 一定有 kernfs 节点 */
	return kn != 0;
}

static void cpu_resolve_syms(void)
{
	p_css_task_iter_start =
		(void *)kallrecon_klp("css_task_iter_start");
	p_css_task_iter_next =
		(void *)kallrecon_klp("css_task_iter_next");
	p_css_task_iter_end =
		(void *)kallrecon_klp("css_task_iter_end");
	p_send_sig_info =
		(void *)kallrecon_klp("send_sig_info");
	p_rstat_flush_hold =
		(void *)kallrecon_klp("cgroup_rstat_flush_hold");
	p_rstat_flush_release =
		(void *)kallrecon_klp("cgroup_rstat_flush_release");
	p_task_sched_runtime =
		(void *)kallrecon_klp("task_sched_runtime");
}

/*
 * struct cgroup.bstat 的偏移：编译期 offsetof 以模块自己的头文件为准，
 * 运行内核如果 CONFIG_SCHED_CORE / CGROUP_SUBSYS_COUNT 不同，偏移就错。
 * 用 init_css_set 布局校验（同 droid_lkm_pids 的做法）：
 *   css_set 以 subsys[CGROUP_SUBSYS_COUNT] 开头，数 cgroup_subsys[] 的
 *   实际长度，修正 dfl_cgrp 偏移并校验 init_css_set.dfl_cgrp。
 * bstat 在 cgroup 的尾部区域，前面的 e_csets[CGROUP_SUBSYS_COUNT] 和
 * rstat 字段都受 SUBSYS_COUNT 影响，所以同样修正。
 */
static void __nocfi cpu_layout_init(void)
{
	unsigned long subsys, init_css, dfl_root, init_task_addr;
	unsigned long expect_root, v;
	int phone_count = 0, i;

	subsys = kallrecon_klp("cgroup_subsys");
	init_css = kallrecon_klp("init_css_set");
	dfl_root = kallrecon_klp("cgrp_dfl_root");
	init_task_addr = kallrecon_klp("init_task");

	if (!subsys || !init_css || !dfl_root || !init_task_addr) {
		cpu_warn("layout symbols missing\n");
		return;
	}

	for (i = 0; i < 64; i++) {
		if (!ctl_read_ptr(subsys + i * 8, &v))
			break;
		if (!v || (v & 0xffff000000000000UL) != 0xffff000000000000UL)
			break;
		phone_count++;
	}

	/* 校验 css_set.dfl_cgrp，用修正后的偏移 */
	{
		unsigned long dfl_off = offsetof(struct css_set, dfl_cgrp) +
			(long)(phone_count - (int)CGROUP_SUBSYS_COUNT) * 8;

		expect_root = dfl_root + offsetof(struct cgroup_root, cgrp);
		if (!ctl_read_ptr(init_css + dfl_off, &v) || v != expect_root) {
			cpu_warn("css_set.dfl_cgrp unverified (cnt=%d)\n",
				 phone_count);
			return;
		}
	}

	/*
	 * bstat 修正：cgroup 的 e_csets[CGROUP_SUBSYS_COUNT] 在 bstat 之前，
	 * SUBSYS_COUNT 差多少就修正多少 * 8（指针数组）。
	 */
	ctl_bstat_off = offsetof(struct cgroup, bstat) +
		(long)(phone_count - (int)CGROUP_SUBSYS_COUNT) * 8;
	cpu_info("layout ok: controllers=%d bstat_off=%lu\n",
		 phone_count, ctl_bstat_off);
	cpu_info("syms: flush_hold=%px flush_release=%px\n",
		 p_rstat_flush_hold, p_rstat_flush_release);
}

static void __nocfi cpu_sig_cgroup(struct cgroup *cgrp, int sig)
{
	struct cgroup_subsys_state *css = NULL;
	struct css_task_iter it;
	struct task_struct *tsk;
	struct kernel_siginfo info;

	if (!p_css_task_iter_start || !p_css_task_iter_next ||
	    !p_css_task_iter_end || !p_send_sig_info)
		return;

	/* 用 cgroup 自己的 css（root css）遍历 */
	css = &cgrp->self;
	memset(&it, 0, sizeof(it));
	p_css_task_iter_start(css, 0, &it);
	while ((tsk = p_css_task_iter_next(&it))) {
		memset(&info, 0, sizeof(info));
		info.si_signo = sig;
		info.si_code = SI_KERNEL;
		p_send_sig_info(sig, &info, tsk);
	}
	p_css_task_iter_end(&it);
}

/*
 * 读 cgroup 的 cpu.stat usage_usec。
 * 路径：/sys/fs/cgroup/<kn 路径>/cpu.stat
 * 用 kernfs 路径构造：of->kn 的完整路径由 kernfs_path 提供。
 */
static bool cpu_debug;

module_param_named(debug, cpu_debug, bool, 0444);
MODULE_PARM_DESC(debug, "log sampling state once per second");

/*
 * 读 cgroup 的累计 CPU 时间（ns -> us）。
 * 不走 vfs（SELinux EACCES + hrtimer 上下文 panic 两重教训），
 * 直接读内核内部聚合字段：
 *   cgroup_rstat_flush_hold(cgrp) 刷出聚合值
 *   cgrp->bstat.cputime.sum_exec_runtime 读
 *   cgroup_rstat_flush_release(cgrp)
 */
static u64 __nocfi cpu_read_usage(struct cgroup *cgrp)
{
	struct cgroup_subsys_state *css;
	struct css_task_iter it;
	struct task_struct *tsk;
	unsigned long long total = 0;
	int n = 0;

	if (!p_css_task_iter_start || !p_css_task_iter_next ||
	    !p_css_task_iter_end || !p_task_sched_runtime)
		return 0;

	/* 进程实际活在 ds-enter 等子 cgroup，css_task_iter 只遍历本
	 * cgroup 的 css_set，所以沿 children 递归累加整棵子树 */
	{
		struct cgroup *kids[64];
		int nkids = 0, ki = 0;
		struct cgroup_subsys_state *csss[64];
		struct cgroup *cur = cgrp;

		csss[0] = &cur->self;
		kids[0] = cur;
		nkids = 1;

		while (ki < nkids && ki < 64) {
			struct cgroup *c = kids[ki++];
			struct cgroup_subsys_state *csc = &c->self;

			p_css_task_iter_start(csc, CSS_TASK_ITER_PROCS, &it);
			while ((tsk = p_css_task_iter_next(&it))) {
				unsigned long long rt = p_task_sched_runtime(tsk);

				total += rt;
				n++;
				if (cpu_debug && n <= 2) {
					static unsigned long last_rt;
					if (time_after(jiffies, last_rt + 3 * HZ)) {
						last_rt = jiffies;
						pr_info("[droid_lkm_cpu] task %s pid=%d rt=%llu\n",
							tsk->comm, tsk->pid, rt);
					}
				}
				if (n > 4096)
					break;
			}
			p_css_task_iter_end(&it);

			/* 收集子 cgroup（BFS，防深度过大） */
			{
				struct cgroup *child;
				struct cgroup_subsys_state *cc;

				rcu_read_lock();
				list_for_each_entry(cc, &c->self.children,
						    sibling) {
					if (nkids >= 64)
						break;
					child = cc->cgroup;
					kids[nkids++] = child;
				}
				rcu_read_unlock();
			}
			if (n > 4096)
				break;
		}
		(void)csss;
		if (cpu_debug) {
			static unsigned long last_nk;
			if (time_after(jiffies, last_nk + 3 * HZ)) {
				last_nk = jiffies;
				pr_info("[droid_lkm_cpu] cgrp=%px subtree: kids=%d tasks=%d total=%llu\n",
					cgrp, nkids, n, total);
			}
		}
	}

	if (cpu_debug && n == 0) {
		static unsigned long last_empty;
		if (time_after(jiffies, last_empty + 3 * HZ)) {
			last_empty = jiffies;
			pr_info("[droid_lkm_cpu] cgrp=%px has no tasks\n",
				cgrp);
		}
	}
	return (u64)(total / 1000);
}

/* ---------------------------------------------------------- 采样循环 ---
 *
 * 注意：采样不能跑在 hrtimer 中断上下文里。filp_open/kernel_read 走完整
 * vfs 路径（path_openat → 锁、睡眠、用户地址访问），在中断上下文调用会
 * do_mem_abort 导致 kernel panic（本机已实测：cpu_tick+0x238 filp_open
 * → die_kernel_fault）。用 delayed_work 让采样在进程上下文执行。
 */

static struct delayed_work cpu_tick_work;
static bool cpu_tick_running;

static void __nocfi cpu_tick(struct work_struct *w)
{
	struct cpu_priv *p;
	unsigned long idx;
	u64 now = ktime_to_us(ktime_get());

	if (cpu_debug) {
		static unsigned long last_count;
		int cnt = 0;

		xa_for_each(&cpu_xa, idx, p) {
			cnt++;
			if (time_after(jiffies, last_count + 5 * HZ)) {
				pr_info("[droid_lkm_cpu] entry[%d] cgrp=%px quota=%lld\n",
					cnt, (void *)idx,
					(long long)atomic64_read(&p->quota_us));
			}
		}
		if (time_after(jiffies, last_count + 5 * HZ)) {
			last_count = jiffies;
			pr_info("[droid_lkm_cpu] xarray entries=%d\n", cnt);
		}
	}

	xa_for_each(&cpu_xa, idx, p) {
		u64 quota = atomic64_read(&p->quota_us);
		u64 period = atomic64_read(&p->period_us);
		u64 wstart;
		struct cgroup *cgrp = (struct cgroup *)idx;

		if (quota <= 0 || quota == S64_MAX)
			continue;

		/* 已 rmdir（kprobe 置位）或已不可用：摘条目并回收。
		 * 这里在 workqueue 上下文，可以 synchronize_rcu()。 */
		if (atomic_read(&p->dead) || !ctl_cgrp_alive(cgrp)) {
			bool removed = false;

			mutex_lock(&cpu_lock);
			if (xa_load(&cpu_xa, idx) == p) {
				xa_erase(&cpu_xa, idx);
				removed = true;
			}
			mutex_unlock(&cpu_lock);
			if (cpu_debug)
				pr_info("[droid_lkm_cpu] drop cgrp=%px removed=%d\n",
					cgrp, (int)removed);
			/* 索引被别人摘走时，那份引用由摘除方负责释放 */
			if (removed)
				cpu_free_entry(idx, p);
			continue;
		}

		{
			u64 total = cpu_read_usage(cgrp);
			u64 wbase = atomic64_read(&p->window_usage_base);
			u64 wused;

			if (cpu_debug && !total) {
				static unsigned long last_zero;
				if (time_after(jiffies, last_zero + 3 * HZ)) {
					last_zero = jiffies;
					pr_info("[droid_lkm_cpu] cgrp=%px total=0 (read fail?)\n",
						cgrp);
				}
			}

			if (!wbase) {
				/* 第一次采样：以当前累计值为基准 */
				atomic64_set(&p->window_usage_base, total);
				atomic64_set(&p->window_start, now);
				continue;
			}

			wstart = atomic64_read(&p->window_start);
			if (wstart && now - wstart >= period) {
				/* 窗口结束：重置基准并解冻 */
				atomic64_set(&p->window_usage_base, total);
				atomic64_set(&p->window_start, now);
				atomic64_set(&p->used_us, 0);
				if (atomic64_read(&p->frozen)) {
					atomic64_set(&p->frozen, 0);
					cpu_info("cgroup %px window reset, "
						 "SIGCONT\n", cgrp);
					cpu_sig_cgroup(cgrp, SIGCONT);
				}
				continue;
			}

			wused = total >= wbase ? total - wbase : 0;
			atomic64_set(&p->used_us, wused);

			if (cpu_debug) {
				/* 每个 cgroup 独立节流打印 */
				static unsigned long last_dbg[8];
				static unsigned long dbg_cg[8];
				unsigned long cgv = (unsigned long)cgrp;
				int slot = -1, k;

				for (k = 0; k < 8; k++) {
					if (dbg_cg[k] == cgv) {
						slot = k;
						break;
					}
					if (dbg_cg[k] == 0 && slot < 0)
						slot = k;
				}
				if (slot >= 0) {
					dbg_cg[slot] = cgv;
					if (time_after(jiffies, last_dbg[slot] + HZ)) {
						last_dbg[slot] = jiffies;
						pr_info("[droid_lkm_cpu] dbg cgrp=%px window_used=%llu quota=%llu frozen=%lld\n",
							cgrp, (unsigned long long)wused,
							(unsigned long long)quota,
							(long long)atomic64_read(&p->frozen));
					}
				}
			}
			if (wused >= quota && !atomic64_read(&p->frozen)) {
				atomic64_set(&p->frozen, 1);
				cpu_warn("cgroup %px over quota (%llu >= %llu), "
					 "SIGSTOP\n", cgrp, wused, quota);
				cpu_sig_cgroup(cgrp, SIGSTOP);
			}
		}
	}

	schedule_delayed_work(&cpu_tick_work,
			      msecs_to_jiffies(CPU_TICK_MS));
}

/* ------------------------------------------------- cgroup_rmdir 清理 --- */

static struct kprobe ctl_rmdir_probe;
static bool ctl_rmdir_live;

/*
 * 容器重启会 rmdir 掉旧 cgroup，但 xarray 里存的是裸指针。这里在删除
 * 时主动清条目，是防 panic 的第一道防线（第二道是 tick 里的
 * ctl_cgrp_alive 校验）。
 */
static int __nocfi ctl_rmdir_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct kernfs_node *kn = (struct kernfs_node *)regs->regs[0];
	unsigned long v;
	struct cpu_priv *cp;

	if (!kn)
		return 0;
	if (!ctl_read_ptr((unsigned long)kn + ctl_kn_priv_off, &v))
		return 0;

	/*
	 * [安全] kprobe 上下文里不能做 xa_erase/kfree：
	 *   - 会与 tick 遍历并发，造成释放后使用；
	 *   - kfree 之后 tick 里那只指针就野了。
	 * 这里只置 dead 标记（原子操作、不睡眠），真正回收交给 tick
	 * 工作队列，那里可以安全地摘条目、drop 引用并 synchronize_rcu。
	 */
	rcu_read_lock();
	cp = xa_load(&cpu_xa, v);
	if (cp) {
		atomic_set(&cp->dead, 1);
		if (cpu_debug)
			pr_info("[droid_lkm_cpu] rmdir cgrp=%lx marked dead\n", v);
	}
	rcu_read_unlock();
	return 0;
}

static void cpu_install_rmdir_probe(void)
{
	int ret;

	ctl_kn_priv_off = offsetof(struct kernfs_node, priv);
	ctl_rmdir_probe.symbol_name = "cgroup_rmdir";
	ctl_rmdir_probe.pre_handler = ctl_rmdir_pre;
	ret = register_kprobe(&ctl_rmdir_probe);
	if (ret) {
		cpu_warn("cgroup_rmdir probe failed: %d "
			 "(dead-entry check stays as the only guard)\n", ret);
		return;
	}
	ctl_rmdir_live = true;
	cpu_info("probed cgroup_rmdir\n");
}

/* ---------------------------------------------------------- init/exit -- */

static struct cgroup_subsys *memory_ss;
static bool cpu_files_live;

/* --------------------------------------------------- sched sysctl 补位 ---
 *
 * Droidspaces 判定 CPU limit 可用性的条件是：
 *
 *   access("/proc/sys/kernel/sched_cfs_bandwidth_slice_us", F_OK) == 0 &&
 *       ds_cgroup_has_controller("cpu")
 *
 * 第二项由显示层（cgroup.controllers 里补 "cpu"）满足，第一项不行：
 * 这个 sysctl 是 CONFIG_CFS_BANDWIDTH 保护下的注册项，config=n 时内核
 * 根本不建这个文件，所以 access() 必然 ENOENT，--cpus 被跳过。
 *
 * 内核把它定义在 kernel/sched/fair.c 的 sched_fair_sysctl 表里：
 *
 *     .procname     = "sched_cfs_bandwidth_slice_us",
 *     .data         = &sysctl_sched_cfs_bandwidth_slice,   // 默认 5000
 *     .maxlen       = sizeof(unsigned int),
 *     .mode         = 0644,
 *     .proc_handler = proc_dointvec_minmax,
 *     .extra1       = SYSCTL_ONE,
 *
 * register_sysctl() 是 EXPORT_SYMBOL，模块可以真的注册一张同名表，
 * 于是那个路径会真实存在、可读可写，而不是靠伪造 stat 骗过 access()。
 *
 * 设备的 CONFIG_RANDSTRUCT_NONE=y，struct ctl_table 布局可靠，
 * 可以静态初始化。若哪天换成 RANDSTRUCT_FULL，这里会读到错位的
 * 字段——所以只在运行时校验 layout 之后再注册（见 cpu_sysctl_register）。
 */
static unsigned int cpu_cfs_slice_us = 5000;

static struct ctl_table cpu_sched_sysctls[] = {
	{
		.procname	= "sched_cfs_bandwidth_slice_us",
		.data		= &cpu_cfs_slice_us,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE,
	},
	{}
};

static struct ctl_table_header *cpu_sysctl_hdr;

static void cpu_sysctl_register(void)
{
	struct path p;

	if (cpu_sysctl_hdr)
		return;
	/* 只补上真正缺的那一项；内核自己注册过就别重复建 */
	if (kern_path("/proc/sys/kernel/sched_cfs_bandwidth_slice_us",
		      LOOKUP_FOLLOW, &p) == 0) {
		path_put(&p);
		cpu_info("sched_cfs_bandwidth_slice_us already present\n");
		return;
	}
	cpu_sysctl_hdr = register_sysctl("kernel", cpu_sched_sysctls);
	if (IS_ERR_OR_NULL(cpu_sysctl_hdr)) {
		cpu_warn("register_sysctl failed: %ld\n",
			 cpu_sysctl_hdr ? PTR_ERR(cpu_sysctl_hdr) : -ENOMEM);
		cpu_sysctl_hdr = NULL;
		return;
	}
	cpu_info("registered sched_cfs_bandwidth_slice_us\n");
}

static int __init droid_lkm_cpu_init(void)
{
	unsigned long mem_ss_addr;
	int ret;

	find_kallsyms_base();
	if (!klnum_val || !kallrecon_klp) {
		cpu_err("kallsyms recovery failed\n");
		return -ENODATA;
	}

	mem_ss_addr = kallrecon_klp("memory_cgrp_subsys");
	if (!mem_ss_addr) {
		cpu_err("memory_cgrp_subsys not found\n");
		return -ENODEV;
	}
	memory_ss = (struct cgroup_subsys *)mem_ss_addr;

	cpu_resolve_syms();
	cpu_layout_init();

	ret = cgroup_add_dfl_cftypes(memory_ss, cpu_max_files);
	if (ret)
		cpu_err("cgroup_add_dfl_cftypes failed: %d\n", ret);
	else
		cpu_files_live = true;

	cpu_install_rmdir_probe();
	cpu_sysctl_register();

	INIT_DELAYED_WORK(&cpu_tick_work, cpu_tick);
	schedule_delayed_work(&cpu_tick_work, msecs_to_jiffies(CPU_TICK_MS));
	cpu_tick_running = true;

	cpu_info("cpu.max emulation active (files=%d)\n", cpu_files_live);
	return 0;
}

static void __exit droid_lkm_cpu_exit(void)
{
	struct cpu_priv *p;
	unsigned long idx;

	if (cpu_tick_running) {
		cancel_delayed_work_sync(&cpu_tick_work);
		cpu_tick_running = false;
	}
	if (ctl_rmdir_live)
		unregister_kprobe(&ctl_rmdir_probe);
	if (cpu_sysctl_hdr) {
		unregister_sysctl_table(cpu_sysctl_hdr);
		cpu_sysctl_hdr = NULL;
	}
	if (cpu_files_live)
		cgroup_rm_cftypes(cpu_max_files);

	/* 逐个摘除并释放：必须先 drop 我们持有的 cgroup 引用，
	 * 否则对应 cgroup 永远回收不了（内存泄漏）。 */
	{
		struct cpu_priv *victim;

		xa_for_each(&cpu_xa, idx, p) {
			mutex_lock(&cpu_lock);
			victim = xa_erase(&cpu_xa, idx);
			mutex_unlock(&cpu_lock);
			if (victim)
				cpu_free_entry(idx, victim);
		}
	}
	xa_destroy(&cpu_xa);
	cpu_info("unloaded\n");
}

module_init(droid_lkm_cpu_init);
module_exit(droid_lkm_cpu_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("emulated cpu.max for a kernel built without CONFIG_CFS_BANDWIDTH");
MODULE_AUTHOR("misaka5202");
