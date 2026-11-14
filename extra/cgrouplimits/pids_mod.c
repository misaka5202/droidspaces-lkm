// SPDX-License-Identifier: GPL-2.0-only
/*
 * droid_lkm_pids — pids 控制器仿真（6.1 移植版）
 *
 * 移植自 ai-luo2006/droidspaces-6.12-pmb110 的 Plan B 设计，针对
 * OPPO Find X7 / MT6989 / 6.1.157-android14-11 / KMI android14-6.1。
 *
 * 与 6.12 原版的差异：
 *   - 不依赖 HooKern / Type_info / KernCall 框架，用标准 register_kprobe()；
 *   - hk_regs_return() 等价为 regs->pc = regs->regs[30]（arm64 LR）；
 *   - 显示层用 kretprobe 追加输出，不用 inline hook；
 *   - KallRecon 复用本仓库 net_ip6nat/kr/ 的 6.1 版。
 *
 * 三层设计（与原版一致）：
 *   显示层   kretprobe 在 cgroup_controllers_show / proc_cgroupstats_show
 *            返回前追加 "pids"，让 Droidspaces 的检查项变绿
 *   文件层   cgroup_add_dfl_cftypes() 在 memory 控制器上挂
 *            pids.max / pids.current / pids.stats
 *   执行层   kprobe 拦 cgroup_can_fork，超限时写 -EAGAIN 并直接返回
 *            （函数体不执行），cgroup_post_fork 记账 +1，
 *            cgroup_exit 记账 -1，cgroup_rmdir 清状态
 *
 * 加载方式：insmod，不设开机自启。所有探针 best-effort，
 * 装不上就跳过，内核保持原样，绝不 panic。
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
#include <linux/smp.h>

#include "kr/core.h"

#define TAG "droid_lkm_pids"
#define pids_info(fmt, ...)  pr_info("[" TAG "] " fmt, ##__VA_ARGS__)
#define pids_warn(fmt, ...)  pr_warn("[" TAG "] " fmt, ##__VA_ARGS__)
#define pids_err(fmt, ...)   pr_err("[" TAG "] " fmt, ##__VA_ARGS__)

/* ---------------------------------------------------------------- 状态 --- */

/* per-cgroup 状态，key 是 struct cgroup * */
struct pids_priv {
	atomic64_t counter;	/* 本 cgroup + 所有后代（层级累加） */
	atomic64_t limit;	/* 0=未启用; S64_MAX="max" 无限 */
	atomic_t dead;		/* 1=cgroup 已 rmdir，等待回收 */
};

static DEFINE_XARRAY(pids_xa);

/*
 * [安全] 条目生命周期（与 cpu 模块同样的三处缺陷）。
 *
 * 原实现：xarray 存裸 struct cgroup *，cgroup_rmdir kprobe 里直接
 * xa_erase，而且**没有 kfree** —— 每条 pids_priv 永久泄漏。更麻烦的是
 * cgroup 释放后指针可能被新 cgroup 复用，新容器就会意外继承上一个容器
 * 的 pids 限制。
 *
 * 现在：建条目时用 percpu_ref_get(&cgrp->self.refcnt) 抓住 cgroup（等价
 * 内核 cgroup_get()），条目存活期间该地址不会被复用；kprobe 只置 dead，
 * 由 reaper 工作队列摘条目、drop 引用并按 RCU 安全释放。
 */
static DEFINE_MUTEX(pids_lock);

static void pids_cgroup_get(struct cgroup *cgrp)
{
	if (!(READ_ONCE(cgrp->self.flags) & CSS_NO_REF))
		percpu_ref_get(&cgrp->self.refcnt);
}

static void pids_cgroup_put(struct cgroup *cgrp)
{
	if (!(READ_ONCE(cgrp->self.flags) & CSS_NO_REF))
		percpu_ref_put(&cgrp->self.refcnt);
}

/* 索引已摘除；可睡眠上下文调用 */
static void pids_free_entry(unsigned long idx, struct pids_priv *p)
{
	pids_cgroup_put((struct cgroup *)idx);
	synchronize_rcu();
	kfree(p);
}

/* 读方进入 rcu_read_lock() 后可安全解引用 */
static struct pids_priv *pids_find(struct cgroup *cgrp)
{
	return xa_load(&pids_xa, (unsigned long)cgrp);
}

#define pids_next_ancestor(c)	((c)->self.parent ? (c)->self.parent->cgroup : NULL)

/* 层级检查：沿祖先链，任何一个祖先超限就 -EAGAIN */
static int pids_check(struct cgroup *cgrp)
{
	struct cgroup *c;

	for (c = cgrp; c; c = pids_next_ancestor(c)) {
		struct pids_priv *p = pids_find(c);
		s64 limit;

		if (!p)
			continue;
		limit = atomic64_read(&p->limit);
		if (limit <= 0 || limit == S64_MAX)
			continue;
		if (atomic64_read(&p->counter) + 1 > limit)
			return -EAGAIN;
	}
	return 0;
}

/* 层级记账：+1 或 -1，沿祖先链累加 */
static void pids_charge(struct cgroup *cgrp, int add)
{
	struct cgroup *c;

	for (c = cgrp; c; c = pids_next_ancestor(c)) {
		struct pids_priv *p = pids_find(c);
		long v;

		if (!p)
			continue;
		if (atomic64_read(&p->limit) <= 0)
			continue;
		v = atomic64_add_return(add, &p->counter);
		if (v < 0)
			atomic64_set(&p->counter, 0);
	}
}

/* ------------------------------------------------------------ 布局校验 ---
 *
 * struct css_set 以 subsys[CGROUP_SUBSYS_COUNT] 开头，dfl_cgrp 的偏移取决于
 * 运行内核编译了多少个控制器。运行时从 cgroup_subsys[] 数出实际个数，换算
 * 偏移后双重校验。校验失败就不装 fork/exit 探针，只留显示层和文件层。
 */

static unsigned long ctl_css_dfl_cgrp_off;
static unsigned long ctl_task_cgroups_off;
static unsigned long ctl_kargs_cset_off;
static unsigned long ctl_kn_priv_off;
static bool ctl_layout_ok;

static bool ctl_read_ptr(unsigned long addr, unsigned long *out)
{
	return copy_from_kernel_nofault(out, (void *)addr, sizeof(*out)) == 0;
}

static bool ctl_is_kptr(unsigned long v)
{
	return v && (v & 0xffff000000000000UL) == 0xffff000000000000UL;
}

static void ctl_layout_init(void)
{
	unsigned long subsys, init_css, dfl_root, init_task_addr;
	unsigned long expect_root, v;
	int phone_count = 0;
	int i;

	subsys = kallrecon_klp("cgroup_subsys");
	init_css = kallrecon_klp("init_css_set");
	dfl_root = kallrecon_klp("cgrp_dfl_root");
	init_task_addr = kallrecon_klp("init_task");

	if (!subsys || !init_css || !dfl_root || !init_task_addr) {
		pids_warn("layout symbols missing (subsys=%d init_css=%d "
			  "dfl_root=%d init_task=%d)\n",
			  !!subsys, !!init_css, !!dfl_root, !!init_task_addr);
		return;
	}

	/* 数 cgroup_subsys[] 里的指针数量：内核实际编进的控制器数 */
	for (i = 0; i < 64; i++) {
		if (!ctl_read_ptr(subsys + i * 8, &v))
			break;
		if (!ctl_is_kptr(v))
			break;
		phone_count++;
	}

	/* offsetof 是编译期的（模块按自己的头文件编译），修正为运行时实际值 */
	ctl_css_dfl_cgrp_off = offsetof(struct css_set, dfl_cgrp) +
		(long)(phone_count - (int)CGROUP_SUBSYS_COUNT) * 8;
	ctl_task_cgroups_off = offsetof(struct task_struct, cgroups);
	ctl_kargs_cset_off = offsetof(struct kernel_clone_args, cset);
	ctl_kn_priv_off = offsetof(struct kernfs_node, priv);

	/* 校验 1：init_css_set.dfl_cgrp 必须等于 &cgrp_dfl_root.cgrp */
	expect_root = dfl_root + offsetof(struct cgroup_root, cgrp);
	if (!ctl_read_ptr(init_css + ctl_css_dfl_cgrp_off, &v) ||
	    v != expect_root) {
		pids_warn("css_set.dfl_cgrp unverified: controllers=%d "
			  "off=%lu got=0x%lx want=0x%lx\n",
			  phone_count, ctl_css_dfl_cgrp_off, v, expect_root);
		return;
	}

	/* 校验 2：init_task.cgroups 必须等于 &init_css_set */
	if (!ctl_read_ptr(init_task_addr + ctl_task_cgroups_off, &v) ||
	    v != init_css) {
		pids_warn("task_struct.cgroups unverified: off=%lu "
			  "got=0x%lx want=0x%lx\n",
			  ctl_task_cgroups_off, v, init_css);
		return;
	}

	ctl_layout_ok = true;
	pids_info("layout verified: controllers=%d (built against %d)\n",
		  phone_count, (int)CGROUP_SUBSYS_COUNT);
}

static struct cgroup *ctl_css_dfl_cgrp(struct css_set *cset)
{
	unsigned long v;

	if (!ctl_layout_ok || !cset)
		return NULL;
	if (!ctl_read_ptr((unsigned long)cset + ctl_css_dfl_cgrp_off, &v))
		return NULL;
	return (struct cgroup *)v;
}

static struct cgroup *ctl_task_dfl_cgrp(struct task_struct *tsk)
{
	unsigned long v;

	if (!ctl_layout_ok || !tsk)
		return NULL;
	if (!ctl_read_ptr((unsigned long)tsk + ctl_task_cgroups_off, &v))
		return NULL;
	return ctl_css_dfl_cgrp((struct css_set *)v);
}

/* ------------------------------------------------------------ 诊断计数 --- */

static atomic_t ctl_n_subtree;
static atomic_t ctl_n_strip;
static atomic_t ctl_n_rewrite;
static atomic_t ctl_n_fork;
static atomic_t ctl_n_charged;
static atomic_t ctl_n_refused;
static atomic_t ctl_n_rmdir;

/* -------------------------------------------------- B3 subtree 翻译 --- */

static const char *const ctl_strip_names[] = { "pids", "cpu" };

static void ctl_blank_name(char *buf, size_t nbytes, const char *name)
{
	size_t nlen = strlen(name);
	size_t i;

	for (i = 0; i + nlen <= nbytes; i++) {
		size_t start = i;
		size_t end = i + nlen;

		if (memcmp(buf + i, name, nlen) != 0)
			continue;
		/* token 边界：cpuset 不能被削成 set */
		if (end < nbytes && buf[end] != ' ' && buf[end] != '\n' &&
		    buf[end] != '\t')
			continue;
		if (start > 0 && (buf[start - 1] == '+' || buf[start - 1] == '-'))
			start--;
		memset(buf + start, ' ', end - start);
		atomic_inc(&ctl_n_strip);
		i = end - 1;
	}
}

/*
 * 重写目标 buffer。不能用 per-CPU buffer：
 *   preempt_enable() 后函数体(strstrip/strsep)执行时进程可能已迁移到别的
 *   CPU，per-CPU 槽位被同函数的新调用覆盖，strsep 读不到 NUL 就越界，
 *   FORTIFY 报 count=SIZE_MAX 然后 SIGABRT。
 * 改成全局 buffer：cgroup.subtree_control 的写路径全程持 cgroup_mutex，
 * 天然串行，不存在并发覆盖。
 */
static char ctl_rw_buf[32];

#define CTL_REWRITTEN_MAX 8
static unsigned long ctl_rewritten[CTL_REWRITTEN_MAX];
static int ctl_n_rewritten;

static bool ctl_already_rewritten(unsigned long cgrp)
{
	int i;

	for (i = 0; i < ctl_n_rewritten; i++)
		if (ctl_rewritten[i] == cgrp)
			return true;
	return false;
}

static void ctl_note_rewritten(unsigned long cgrp)
{
	if (ctl_n_rewritten >= CTL_REWRITTEN_MAX || !cgrp)
		return;
	ctl_rewritten[ctl_n_rewritten++] = cgrp;
}

static struct kprobe ctl_subtree_probe;

static int ctl_subtree_pre(struct kprobe *p, struct pt_regs *regs)
{
	char *buf = (char *)regs->regs[1];
	size_t nbytes = (size_t)regs->regs[2];
	struct kernfs_open_file *of = (struct kernfs_open_file *)regs->regs[0];
	unsigned long cgrp = 0;
	bool saw_pids = false;
	bool has_enable = false;
	size_t i;
	int j;

	atomic_inc(&ctl_n_subtree);

	if (!buf || !nbytes || nbytes > PAGE_SIZE)
		return 0;

	if (of && of->kn && of->kn->parent)
		cgrp = (unsigned long)of->kn->parent->priv;

	for (i = 0; i + 4 <= nbytes; i++) {
		if (memcmp(buf + i, "pids", 4) != 0)
			continue;
		if (i > 0 && (buf[i - 1] == '+' || buf[i - 1] == '-')) {
			saw_pids = true;
			break;
		}
	}

	for (j = 0; j < ARRAY_SIZE(ctl_strip_names); j++)
		ctl_blank_name(buf, nbytes, ctl_strip_names[j]);

	for (i = 0; i < nbytes; i++) {
		if (buf[i] != ' ' && buf[i] != '\n' && buf[i] != '\t' &&
		    buf[i] != '\0') {
			has_enable = true;
			break;
		}
	}
	if (has_enable)
		return 0;

	/* 只剩 +pids 这类"我们发明的控制器" */
	if (!saw_pids)
		return 0;

	/* 已被翻译过（= 这个 cgroup 的 memory 已由我们启用）：再 +pids
	 * 就无事成功。清空 buffer，函数体 strstrip 得空串直接返回 0。 */
	if (cgrp && ctl_already_rewritten(cgrp)) {
		memset(buf, ' ', nbytes);
		return 0;
	}

	/* 用户先写过 +memory（不经我们）的 cgroup：memory 已启用，
	 * 再 rewrite 会重复 enable 得 EFAULT。用 kernfs 检查子文件。 */
	if (cgrp && of && of->kn && of->kn->parent) {
		struct kernfs_node *mc =
			kernfs_find_and_get(of->kn->parent, "memory.current");

		if (mc) {
			kernfs_put(mc);
			memset(buf, ' ', nbytes);
			ctl_note_rewritten(cgrp);
			return 0;
		}
	}

	/* 全局 buffer：写路径由 cgroup_mutex 串行，无需 per-CPU */
	memcpy(ctl_rw_buf, "+memory", 8);
	regs->regs[1] = (unsigned long)ctl_rw_buf;
	regs->regs[2] = 7;
	ctl_note_rewritten(cgrp);
	atomic_inc(&ctl_n_rewrite);

	return 0;
}

/* ---------------------------------------------------- B2 fork 拦截 --- */

static struct kprobe ctl_can_fork_probe;

static int ctl_can_fork_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct cgroup *cgrp;

	atomic_inc(&ctl_n_fork);

	if (!ctl_layout_ok)
		return 0;

	/* 6.1 的 cgroup_can_fork(child, kargs)：entry 时 kargs->cset 还没填好，
	 * 但 current 就是 fork 者，其 cgroup 就是 child 将继承的 cgroup */
	cgrp = ctl_task_dfl_cgrp(current);
	if (!cgrp)
		return 0;
	atomic_inc(&ctl_n_charged);
	if (!pids_check(cgrp))
		return 0;

	/* 超限：写 -EAGAIN 到 x0，pc 跳到 lr，函数体不执行 */
	atomic_inc(&ctl_n_refused);
	regs->regs[0] = (unsigned long)(-EAGAIN);
	regs->pc = regs->regs[30];	/* arm64: hk_regs_return 等价物 */
	return 1;
}

static struct kprobe ctl_fork_probe;

static int ctl_fork_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct kernel_clone_args *kargs =
		(struct kernel_clone_args *)regs->regs[1];
	unsigned long v;
	struct cgroup *cgrp;

	if (!ctl_layout_ok || !kargs)
		return 0;

	/* 6.12 注释同款：entry 时 child->cgroups 还没赋值，kargs->cset 是
	 * cgroup_can_fork() 内部已填好的值 */
	if (!ctl_read_ptr((unsigned long)kargs + ctl_kargs_cset_off, &v))
		return 0;

	cgrp = ctl_css_dfl_cgrp((struct css_set *)v);
	if (cgrp)
		pids_charge(cgrp, 1);
	return 0;
}

static struct kprobe ctl_exit_probe;

static int ctl_exit_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct task_struct *tsk = (struct task_struct *)regs->regs[0];
	struct cgroup *cgrp;

	if (!ctl_layout_ok || !tsk)
		return 0;

	cgrp = ctl_task_dfl_cgrp(tsk);
	if (cgrp)
		pids_charge(cgrp, -1);
	return 0;
}

static struct kprobe ctl_rmdir_probe;

static int ctl_rmdir_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct kernfs_node *kn = (struct kernfs_node *)regs->regs[0];
	unsigned long v;

	atomic_inc(&ctl_n_rmdir);
	if (!kn)
		return 0;
	if (!ctl_read_ptr((unsigned long)kn + ctl_kn_priv_off, &v))
		return 0;

	/*
	 * [安全] 只置标记。kprobe 上下文里不能 xa_erase/kfree：
	 * erase 会让并发读方拿到的 p 立刻失效，而原实现连 kfree 都没有，
	 * 每条记录直接泄漏。真正的摘除/释放交给 pids_reap_work。
	 */
	rcu_read_lock();
	{
		struct pids_priv *cp = xa_load(&pids_xa, v);

		if (cp)
			atomic_set(&cp->dead, 1);
	}
	rcu_read_unlock();
	return 0;
}

/* ------------------------------------------------ dead 条目回收队列 --- */

/*
 * cgroup_rmdir 探针只置 dead 标记；这里周期性把标记过的条目摘掉，
 * drop cgroup 引用并按 RCU 安全释放。用工作队列而不是在 kprobe 里做，
 * 是因为 synchronize_rcu() 会睡眠。
 */
static struct delayed_work pids_reap_work;
static bool pids_reap_live;
#define PIDS_REAP_MS 1000

static void pids_reap_fn(struct work_struct *w)
{
	struct pids_priv *p;
	unsigned long idx;

	xa_for_each(&pids_xa, idx, p) {
		struct pids_priv *victim;

		if (!atomic_read(&p->dead))
			continue;
		mutex_lock(&pids_lock);
		victim = xa_erase(&pids_xa, idx);
		mutex_unlock(&pids_lock);
		if (victim)
			pids_free_entry(idx, victim);
	}
	schedule_delayed_work(&pids_reap_work, msecs_to_jiffies(PIDS_REAP_MS));
}

static void pids_start_reaper(void)
{
	INIT_DELAYED_WORK(&pids_reap_work, pids_reap_fn);
	pids_reap_live = true;
	schedule_delayed_work(&pids_reap_work, msecs_to_jiffies(PIDS_REAP_MS));
}

/* ------------------------------------------------------- B1 文件层 --- */

static u64 pids_max_read_u64(struct cgroup_subsys_state *css,
			     struct cftype *cft)
{
	struct cgroup *cgrp = css->cgroup;
	struct pids_priv *p;
	u64 v = 0;

	rcu_read_lock();
	p = pids_find(cgrp);
	if (p)
		v = (u64)atomic64_read(&p->limit);
	rcu_read_unlock();
	return v;
}

static ssize_t pids_max_write(struct kernfs_open_file *of, char *buf,
			      size_t nbytes, loff_t off)
{
	struct cgroup *cgrp = of->kn->parent->priv;
	char tmp[32];
	size_t i = 0, j = 0;
	s64 val;
	struct pids_priv *p;

	while (i < nbytes && j < sizeof(tmp) - 1 && buf[i] != '\n')
		tmp[j++] = buf[i++];
	tmp[j] = '\0';
	while (j > 0 && (tmp[j - 1] == ' ' || tmp[j - 1] == '\t'))
		tmp[--j] = '\0';

	if (strcmp(tmp, "max") == 0) {
		val = S64_MAX;
	} else {
		char *end;
		long v = simple_strtol(tmp, &end, 10);

		if (end == tmp)
			return -EINVAL;
		while (*end == ' ' || *end == '\t')
			end++;
		if (*end != '\0')
			return -EINVAL;
		val = (s64)v;
		if (val < 0)
			val = 0;
	}

	mutex_lock(&pids_lock);
	p = pids_find(cgrp);
	if (!p) {
		p = kzalloc(sizeof(*p), GFP_KERNEL);
		if (!p) {
			mutex_unlock(&pids_lock);
			return -ENOMEM;
		}
		atomic64_set(&p->limit, 0);
		atomic_set(&p->dead, 0);
		/* 抓住 cgroup：条目存活期间地址不会被复用，避免新容器
		 * 继承上个容器的限制 */
		pids_cgroup_get(cgrp);
		{
			struct pids_priv *old =
				xa_store(&pids_xa, (unsigned long)cgrp, p,
					 GFP_KERNEL);
			if (xa_is_err(old)) {
				int err = xa_err(old);

				pids_cgroup_put(cgrp);
				kfree(p);
				mutex_unlock(&pids_lock);
				return err;
			}
			if (old) {
				pids_cgroup_put(cgrp);
				kfree(p);
				p = old;
			}
		}
	}
	atomic64_set(&p->limit, val);
	atomic_set(&p->dead, 0);
	mutex_unlock(&pids_lock);
	return (ssize_t)nbytes;
}

static u64 pids_current_read_u64(struct cgroup_subsys_state *css,
				 struct cftype *cft)
{
	struct cgroup *cgrp = css->cgroup;
	struct pids_priv *p;
	u64 v = 0;

	rcu_read_lock();
	p = pids_find(cgrp);
	if (p)
		v = (u64)atomic64_read(&p->counter);
	rcu_read_unlock();
	return v;
}

static int pids_stats_show(struct seq_file *sf, void *v)
{
	seq_printf(sf,
		   "subtree=%d strip=%d rewrite=%d fork=%d charged=%d refused=%d "
		   "rmdir=%d layout=%d\n",
		   atomic_read(&ctl_n_subtree), atomic_read(&ctl_n_strip),
		   atomic_read(&ctl_n_rewrite),
		   atomic_read(&ctl_n_fork), atomic_read(&ctl_n_charged),
		   atomic_read(&ctl_n_refused),
		   atomic_read(&ctl_n_rmdir), (int)ctl_layout_ok);
	return 0;
}

static struct cftype pids_max_files[] = {
	{
		.name = "pids.max",
		.flags = CFTYPE_NO_PREFIX | CFTYPE_NOT_ON_ROOT,
		.read_u64 = pids_max_read_u64,
		.write = pids_max_write,
	},
	{
		.name = "pids.current",
		.flags = CFTYPE_NO_PREFIX | CFTYPE_NOT_ON_ROOT,
		.read_u64 = pids_current_read_u64,
	},
	{
		.name = "pids.stats",
		.flags = CFTYPE_NO_PREFIX | CFTYPE_NOT_ON_ROOT,
		.seq_show = pids_stats_show,
	},
	{},
};

/* ------------------------------------------------- 显示层（kretprobe）--- */

static struct kretprobe ctl_ctrl_show_krp;
static struct kretprobe ctl_cgroupstats_krp;

/* cgroup_controllers_show 返回后追加 "pids " */
static int ctl_ctrl_show_ret(struct kretprobe_instance *ri,
			     struct pt_regs *regs)
{
	struct seq_file *m;
	unsigned long mv;

	memcpy(&mv, ri->data, sizeof(mv));
	m = (struct seq_file *)mv;
	if (!m)
		return 0;
	/* 追加两个控制器名：pids（本模块）和 cpu（cpu 模块需要，
	 * Droidspaces 读 cgroup.controllers 判断 v2 路径） */
	seq_puts(m, "pids cpu\n");
	return 0;
}

static int ctl_ctrl_show_entry(struct kretprobe_instance *ri,
			       struct pt_regs *regs)
{
	unsigned long m = regs->regs[0];	/* seq_file* */

	memcpy(ri->data, &m, sizeof(m));
	return 0;
}

/* proc_cgroupstats_show 返回后追加一行 */
static int ctl_cgroupstats_ret(struct kretprobe_instance *ri,
			       struct pt_regs *regs)
{
	struct seq_file *m;
	unsigned long mv;

	memcpy(&mv, ri->data, sizeof(mv));
	m = (struct seq_file *)mv;
	if (!m)
		return 0;
	seq_printf(m, "pids\t0\t1\t1\n");
	return 0;
}

static int ctl_cgroupstats_entry(struct kretprobe_instance *ri,
				 struct pt_regs *regs)
{
	unsigned long m = regs->regs[0];

	memcpy(ri->data, &m, sizeof(m));
	return 0;
}

/* ------------------------------------------------- init/exit --- */

static struct cgroup_subsys *memory_ss;

static bool ctl_live_subtree;
static bool ctl_live_can_fork;
static bool ctl_live_fork;
static bool ctl_live_exit;
static bool ctl_live_rmdir;
static bool ctl_live_files;
static bool ctl_live_display;

static void ctl_probe_one(struct kprobe *h, const char *sym,
			  kprobe_pre_handler_t pre, bool *live)
{
	int ret;

	h->symbol_name = sym;
	h->pre_handler = pre;
	ret = register_kprobe(h);
	if (ret) {
		pids_warn("probe %s failed: %d\n", sym, ret);
		return;
	}
	*live = true;
	pids_info("probed %s\n", sym);
}

static void ctl_unprobe(struct kprobe *h, bool live)
{
	if (live)
		unregister_kprobe(h);
}

static int __init droid_lkm_pids_init(void)
{
	unsigned long mem_ss_addr;
	int ret;

	/* KallRecon 初始化 */
	find_kallsyms_base();
	if (!klnum_val || !kallrecon_klp) {
		pids_err("kallsyms recovery failed\n");
		return -ENODATA;
	}

	/* memory 控制器：pids.max 挂在它上面 */
	mem_ss_addr = kallrecon_klp("memory_cgrp_subsys");
	if (!mem_ss_addr) {
		pids_err("memory_cgrp_subsys not found\n");
		return -ENODEV;
	}
	memory_ss = (struct cgroup_subsys *)mem_ss_addr;

	ctl_layout_init();

	/* dead 条目的回收队列（cgroup_rmdir 探针只置标记） */
	pids_start_reaper();

	/* B3：subtree_control 翻译，最先装 */
	ctl_probe_one(&ctl_subtree_probe, "cgroup_subtree_control_write",
		      ctl_subtree_pre, &ctl_live_subtree);

	/* B2：fork 拦截，只在布局校验通过时装 */
	if (ctl_layout_ok) {
		ctl_probe_one(&ctl_can_fork_probe, "cgroup_can_fork",
			      ctl_can_fork_pre, &ctl_live_can_fork);
		ctl_probe_one(&ctl_fork_probe, "cgroup_post_fork",
			      ctl_fork_pre, &ctl_live_fork);
		ctl_probe_one(&ctl_exit_probe, "cgroup_exit", ctl_exit_pre,
			      &ctl_live_exit);
		ctl_probe_one(&ctl_rmdir_probe, "cgroup_rmdir", ctl_rmdir_pre,
			      &ctl_live_rmdir);
	} else {
		pids_warn("fork probe skipped: layout unverified, "
			  "limits will not be enforced\n");
	}

	/* B1：pids.max 文件 */
	ret = cgroup_add_dfl_cftypes(memory_ss, pids_max_files);
	if (ret)
		pids_err("cgroup_add_dfl_cftypes failed: %d\n", ret);
	else
		ctl_live_files = true;

	/* 显示层 */
	memset(&ctl_ctrl_show_krp, 0, sizeof(ctl_ctrl_show_krp));
	ctl_ctrl_show_krp.kp.symbol_name = "cgroup_controllers_show";
	ctl_ctrl_show_krp.entry_handler = ctl_ctrl_show_entry;
	ctl_ctrl_show_krp.handler = ctl_ctrl_show_ret;
	ctl_ctrl_show_krp.maxactive = 16;
	ctl_ctrl_show_krp.data_size = sizeof(void *);

	memset(&ctl_cgroupstats_krp, 0, sizeof(ctl_cgroupstats_krp));
	ctl_cgroupstats_krp.kp.symbol_name = "proc_cgroupstats_show";
	ctl_cgroupstats_krp.entry_handler = ctl_cgroupstats_entry;
	ctl_cgroupstats_krp.handler = ctl_cgroupstats_ret;
	ctl_cgroupstats_krp.maxactive = 16;
	ctl_cgroupstats_krp.data_size = sizeof(void *);

	if (!register_kretprobe(&ctl_ctrl_show_krp) &&
	    !register_kretprobe(&ctl_cgroupstats_krp)) {
		ctl_live_display = true;
		pids_info("display hooks installed\n");
	} else {
		pids_warn("display hooks failed\n");
		unregister_kretprobe(&ctl_ctrl_show_krp);
		unregister_kretprobe(&ctl_cgroupstats_krp);
	}

	if (ctl_live_can_fork && ctl_live_fork && ctl_live_exit &&
	    ctl_live_files)
		pids_info("pids limit enforcement active "
			  "(emulated pids controller)\n");
	else
		pids_warn("pids limit enforcement incomplete: "
			  "fork=%d post=%d exit=%d pids.max=%d subtree=%d "
			  "rmdir=%d display=%d\n",
			  ctl_live_can_fork, ctl_live_fork, ctl_live_exit,
			  ctl_live_files, ctl_live_subtree, ctl_live_rmdir,
			  ctl_live_display);

	return 0;
}

static void __exit droid_lkm_pids_exit(void)
{
	struct pids_priv *p;
	unsigned long idx;

	if (pids_reap_live) {
		cancel_delayed_work_sync(&pids_reap_work);
		pids_reap_live = false;
	}
	if (ctl_live_display) {
		unregister_kretprobe(&ctl_ctrl_show_krp);
		unregister_kretprobe(&ctl_cgroupstats_krp);
	}
	if (ctl_live_files)
		cgroup_rm_cftypes(pids_max_files);
	ctl_unprobe(&ctl_rmdir_probe, ctl_live_rmdir);
	ctl_unprobe(&ctl_exit_probe, ctl_live_exit);
	ctl_unprobe(&ctl_fork_probe, ctl_live_fork);
	ctl_unprobe(&ctl_can_fork_probe, ctl_live_can_fork);
	ctl_unprobe(&ctl_subtree_probe, ctl_live_subtree);

	/* 逐个摘除并释放：必须 drop 持有的 cgroup 引用 */
	{
		struct pids_priv *victim;

		xa_for_each(&pids_xa, idx, p) {
			mutex_lock(&pids_lock);
			victim = xa_erase(&pids_xa, idx);
			mutex_unlock(&pids_lock);
			if (victim)
				pids_free_entry(idx, victim);
		}
	}
	xa_destroy(&pids_xa);
	pids_info("unloaded\n");
}

module_init(droid_lkm_pids_init);
module_exit(droid_lkm_pids_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("emulated pids controller for a kernel built without CONFIG_CGROUP_PIDS");
MODULE_AUTHOR("dere3046 (6.12), ported to 6.1 by misaka5202");
