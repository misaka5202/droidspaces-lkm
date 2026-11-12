/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * misc.h - 从 droid_lkm_misc 抽出 devtmpfs 时用的最小替代头。
 *
 * 原项目的 misc.h 依赖 core.h / misc_proc.h 等一整串头文件,
 * 而 devtmpfs.c 真正用到的只有四个日志宏和一个符号解析函数。
 * 这里只提供这些, 避免把整个 droid_lkm 依赖树拖进来。
 */
#ifndef DROID_LKM_MISC_H
#define DROID_LKM_MISC_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/printk.h>

#include "core.h"

#define DROID_LKM_MISC_TAG "devtmpfs_lkm"

#define droid_lkm_misc_info(fmt, ...) \
	pr_info("[" DROID_LKM_MISC_TAG "] " fmt, ##__VA_ARGS__)
#define droid_lkm_misc_warn(fmt, ...) \
	pr_warn("[" DROID_LKM_MISC_TAG "] " fmt, ##__VA_ARGS__)
#define droid_lkm_misc_err(fmt, ...) \
	pr_err("[" DROID_LKM_MISC_TAG "] " fmt, ##__VA_ARGS__)

extern bool droid_lkm_misc_verbose;
#define droid_lkm_misc_dbg(fmt, ...)                                          \
	do {                                                                  \
		if (droid_lkm_misc_verbose)                                   \
			pr_info("[" DROID_LKM_MISC_TAG "/dbg] " fmt,          \
				##__VA_ARGS__);                               \
	} while (0)

/* 未导出的内核符号按名字解析, 等价于 kallsyms_lookup_name */
static inline unsigned long __nocfi droid_lkm_misc_sym(const char *name)
{
	return kallrecon_klp(name);
}

/*
 * droid_lkm_misc_report - 原项目用它把子功能状态上报到 procfs 供 App 读取。
 * 独立模块没有 procfs 上报面, 做成空实现即可:
 * 真正让 Droidspaces 认可硬件访问的是 /proc/filesystems 里出现 "devtmpfs",
 * 而那由 register_filesystem() 完成, 与这个上报无关。
 */
static inline void droid_lkm_misc_report(const char *feature, const char *state,
					 const char *detail)
{
	pr_info("[" DROID_LKM_MISC_TAG "] %s: %s%s%s\n", feature, state,
		detail ? " - " : "", detail ? detail : "");
}

#endif /* DROID_LKM_MISC_H */
