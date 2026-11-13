# droid_lkm_pids.ko / droid_lkm_cpu.ko — 仿真 pids / cpu.max 控制器（6.1）
#
# pids 移植自 ai-luo2006/droidspaces-6.12-pmb110 的 Plan B，
# cpu 为自研时间片限流。
#
# 构建：make -C <KDIR> M=<这里> modules

obj-m := droid_lkm_pids.o droid_lkm_cpu.o

# pids 主源 pids_mod.c（不与模块同名，避免自依赖）
droid_lkm_pids-y := pids_mod.o kr/core.o kr/access.o kr/discover.o \
	kr/symbol.o kr/slide.o kr/fastboot.o kr/hint.o kr/variant.o kr/anchor.o

# cpu 主源 cpu_mod.c
droid_lkm_cpu-y := cpu_mod.o kr/core.o kr/access.o kr/discover.o \
	kr/symbol.o kr/slide.o kr/fastboot.o kr/hint.o kr/variant.o kr/anchor.o

ccflags-y += -I$(src)
ccflags-y += -Wno-declaration-after-statement -Wno-unused-variable \
	-Wno-unused-function -Wno-strict-prototypes -Wno-gcc-compat \
	-Wno-incompatible-function-pointer-types
