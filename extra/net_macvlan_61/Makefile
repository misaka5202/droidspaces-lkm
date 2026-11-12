# 用 6.1 内核自带的 macvlan 源码编译成独立模块。
#
# 设备 CONFIG_MACVLAN=n, 但源码树里 drivers/net/macvlan.c 是完整的,
# 直接拿 6.1 自己的版本编译, 避免了 6.12 版本的 API 不兼容
# (lltx / ndo_hwtstamp_get / IFLA_MACVLAN_BC_CUTOFF 等在 6.1 都不存在)。
#
# CONFIG_MACVLAN 必须定义为 1: kbuild 的
# obj-$(CONFIG_MACVLAN) 判断, 以及源码里可能的 #if IS_ENABLED
CFLAGS_macvlan.o := -DCONFIG_MACVLAN=1

ccflags-y += -Wno-declaration-after-statement -Wno-unused-variable \
	-Wno-unused-function -Wno-strict-prototypes

obj-m := macvlan.o
