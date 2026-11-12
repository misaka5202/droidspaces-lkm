/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * compat_61.h - 把 6.12 的 VFS 调用约定映射到 6.1。
 *
 * 6.3 引入了 struct mnt_idmap，VFS 的 mnt_idmap 参数从
 *   int vfs_mkdir(struct user_namespace *, struct inode *, ...)
 * 改成
 *   int vfs_mkdir(struct mnt_idmap *, struct inode *, ...)
 * 并引入全局的 nop_mnt_idmap 常量。
 *
 * 本设备是 6.1，只有 struct user_namespace *，没有 nop_mnt_idmap。
 * 因此：
 *   - 把 devtmpfs.c 里的 &nop_mnt_idmap 替换成 &init_user_ns
 *     （6.1 下"不做 id 映射"就是 init_user_ns）
 *   - 结构体成员的类型统一写成 void* 再强转，避免两套签名打架
 *
 * 由 -include 强制插在 devtmpfs.c 的所有 include 之前。
 */
#ifndef DEVTMPFS_COMPAT_61_H
#define DEVTMPFS_COMPAT_61_H

#include <linux/version.h>
#include <linux/user_namespace.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)

/* 6.1: 没有 mnt_idmap，VFS 用 user_namespace 表示"不做映射" */
#define nop_mnt_idmap init_user_ns

#endif /* < 6.3 */

#endif /* DEVTMPFS_COMPAT_61_H */
