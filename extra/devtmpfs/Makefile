# devtmpfs 独立模块: 在 CONFIG_DEVTMPFS=n 的内核上注册 "devtmpfs" 文件系统。
#
# Droidspaces 判定硬件访问是否可用, 依据是 grep /proc/filesystems "devtmpfs",
# 而不是读内核 config。只要注册一个名为 devtmpfs 的文件系统类型,
# App 就会认为硬件访问可用。
#
# 源码来自 ai-luo2006/droidspaces-6.12-pmb110 的 droid_lkm_misc, 抽出为独立模块。
# 三处必要适配:
#   - compat_61.h : 6.3+ 的 nop_mnt_idmap 在 6.1 映射到 init_user_ns
#   - main.c      : 原注册逻辑在 misc_main.c, 抽出后补 module_init/module_exit
#                   (缺它编出的 ko 没有 init_module 符号, insmod 报 ENOENT)
#   - hk/ 全套   : hk_inline.c 依赖 hk_patch.c (hk_resolve/hk_write_text 等)
#                   与 hk_flush.S (hk_flush_icache), 缺一个就是 Unknown symbol

obj-m := devtmpfs_lkm.o
devtmpfs_lkm-y := main.o devtmpfs.o \
	hk/hk_inline.o hk/hk_patch.o hk/hk_flush.o \
	kr/core.o kr/access.o kr/discover.o kr/symbol.o kr/slide.o \
	kr/fastboot.o kr/hint.o kr/variant.o kr/anchor.o

# -include compat_61.h: 插在 devtmpfs.c 所有 include 之前
CFLAGS_devtmpfs.o := -include $(src)/compat_61.h

ccflags-y += -I$(src) -I$(src)/hk -I$(src)/kr
ccflags-y += -Wno-declaration-after-statement -Wno-unused-variable \
	-Wno-unused-function -Wno-strict-prototypes -Wno-missing-prototypes \
	-Wno-incompatible-function-pointer-types
