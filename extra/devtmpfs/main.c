// SPDX-License-Identifier: GPL-2.0-only
/*
 * main.c - devtmpfs_lkm 的模块入口。
 *
 * devtmpfs.c 是从 droid_lkm_misc 抽出来的实现, 本身只提供
 * droid_lkm_misc_devtmpfs_init()/_release()/_exit(), 不包含
 * module_init()/module_exit()。原项目的注册发生在 misc_main.c 里,
 * 抽出后必须在这里补上入口, 否则编出的 ko 没有 init_module 符号,
 * insmod 会直接返回 ENOENT。
 *
 * 模块参数（沿用原 misc_main.c 的命名）:
 *   devtmpfs_probe = 0  正常模式: 注册文件系统 + 启动 kthread + 挂 device hook
 *                  = 1  只 mount/打印/unmount, 用于探测
 *                  = 2  只注册文件系统, 不启动线程和 hook（最安全）
 *   devtmpfs_hook  = 0/1  是否启用 device_add/device_del 内联 hook
 *
 * 对 Droidspaces 而言, 只需要 /proc/filesystems 里出现 "devtmpfs",
 * 那是 register_filesystem() 的直接结果。若担心 hook 在版本不匹配的
 * 内核上出问题, 可以 insmod devtmpfs_lkm.ko devtmpfs_probe=2
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "devtmpfs.h"

static int __init devtmpfs_lkm_init(void)
{
	int ret;

	pr_info("[devtmpfs_lkm] init\n");

	ret = droid_lkm_misc_devtmpfs_init();
	if (ret) {
		pr_err("[devtmpfs_lkm] init failed: %d\n", ret);
		return ret;
	}

	pr_info("[devtmpfs_lkm] ready, check /proc/filesystems\n");
	return 0;
}

static void __exit devtmpfs_lkm_exit(void)
{
	pr_info("[devtmpfs_lkm] exit\n");
	droid_lkm_misc_devtmpfs_release();
	droid_lkm_misc_devtmpfs_exit();
}

module_init(devtmpfs_lkm_init);
module_exit(devtmpfs_lkm_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("devtmpfs for kernels built with CONFIG_DEVTMPFS=n");
MODULE_AUTHOR("dere3046 (original droid_lkm_misc), ported for 6.1");
