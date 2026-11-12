# devtmpfs 独立模块: 在 CONFIG_DEVTMPFS=n 的内核上注册 "devtmpfs" 文件系统。
#
# Droidspaces 的判定是 grep /proc/filesystems "devtmpfs", 而不是读 config,
# 所以只要有一个名为 devtmpfs 的文件系统注册进来, 硬件访问就能启用。
#
# 源码来自 ai-luo2006/droidspaces-6.12-pmb110 的 droid_lkm_misc, 抽出为独立模块。

obj-m := devtmpfs_lkm.o
devtmpfs_lkm-y := devtmpfs.o \
	hk/hk_inline.o \
	kr/core.o kr/access.o kr/discover.o kr/symbol.o kr/slide.o \
	kr/fastboot.o kr/hint.o kr/variant.o kr/anchor.o

ccflags-y += -I$(src) -I$(src)/hk -I$(src)/kr
ccflags-y += -Wno-declaration-after-statement -Wno-unused-variable \
	-Wno-unused-function -Wno-strict-prototypes -Wno-missing-prototypes
