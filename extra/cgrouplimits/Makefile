# droid_lkm_pids.ko / droid_lkm_cpu.ko — 仿真 pids / cpu.max 控制器
#
# pids 移植自 ai-luo2006/droidspaces-6.12-pmb110 的 Plan B；
# cpu 为自研时间片限流（cpu.max + 采样 + SIGSTOP/SIGCONT）。
#
# 构建全部：
#   make -C <KDIR> M=<这里> modules
# 只编一个（CI 逐个出货用，避免另一个失败连带整条构建）：
#   make -C <KDIR> M=<这里> MODULE=droid_lkm_pids modules
#
# 两个模块各自都链入一份 KallRecon（运行时解析未导出符号），
# 所以不能合并成一个 .ko —— 它们要能独立 insmod / rmmod。

# 两个模块共用的 KallRecon 对象
KR_OBJS := kr/core.o kr/access.o kr/discover.o kr/symbol.o kr/slide.o \
	kr/fastboot.o kr/hint.o kr/variant.o kr/anchor.o

# 默认两个都编；CI 传 MODULE=<name> 只编一个
MODULE ?= all

ifeq ($(MODULE),droid_lkm_pids)
obj-m := droid_lkm_pids.o
else ifeq ($(MODULE),droid_lkm_cpu)
obj-m := droid_lkm_cpu.o
else
obj-m := droid_lkm_pids.o droid_lkm_cpu.o
endif

# 主源刻意不与模块同名：kbuild 对"单文件同名模块"会自依赖
droid_lkm_pids-y := pids_mod.o $(KR_OBJS)
droid_lkm_cpu-y  := cpu_mod.o  $(KR_OBJS)

ccflags-y += -I$(src)
ccflags-y += -Wno-declaration-after-statement -Wno-unused-variable \
	-Wno-unused-function -Wno-strict-prototypes -Wno-gcc-compat \
	-Wno-incompatible-function-pointer-types
