// SPDX-License-Identifier: GPL-2.0-only
/*
 * devtmpfs_lkm 的模块元信息。
 *
 * 原项目把这些放在 misc_main.c 里, 抽出 devtmpfs 后单独成文件,
 * 否则 modpost 会报 "missing MODULE_LICENSE()"。
 */
#include <linux/module.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dere3046 (original), ported for 6.1");
MODULE_DESCRIPTION("devtmpfs for kernels built with CONFIG_DEVTMPFS=n");
