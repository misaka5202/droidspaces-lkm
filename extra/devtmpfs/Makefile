# devtmpfs 独立模块: 在 CONFIG_DEVTMPFS=n 的内核上注册 "devtmpfs" 文件系统。
#
# Droidspaces 判定硬件访问是否可用, 依据是 grep /proc/filesystems "devtmpfs",
# 而不是读内核 config。所以只要注册一个名为 devtmpfs 的文件系统类型,
# App 就会认为硬件访问可用。
#
# 源码来自 ai-luo2006/droidspaces-6.12-pmb110 的 droid_lkm_misc, 抽出为独立模块。
# 该源码针对 6.12, 这里做了 6.1 适配: 见 compat_61.h (mnt_idmap -> user_namespace)

obj-m := devtmpfs_lkm.o
devtmpfs_lkm-y := devtmpfs.o \
	hk/hk_inline.o \
	kr/core.o kr/access.o kr/discover.o kr/symbol.o kr/slide.o \
	kr/fastboot.o kr/hint.o kr/variant.o kr/anchor.o

# -include compat_61.h: 在 devtmpfs.c 的所有 include 之前插入,
# 使 6.3+ 才有的 nop_mnt_idmap 在 6.1 上映射到 init_user_ns
CFLAGS_devtmpfs.o := -include $(src)/compat_61.h

ccflags-y += -I$(src) -I$(src)/hk -I$(src)/kr
ccflags-y += -Wno-declaration-after-statement -Wno-unused-variable \
	-Wno-unused-function -Wno-strict-prototypes -Wno-missing-prototypes \
	-Wno-incompatible-function-pointer-types
