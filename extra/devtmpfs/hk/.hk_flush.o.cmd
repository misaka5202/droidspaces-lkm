cmd_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/hk/hk_flush.o := clang -Wp,-MMD,/mnt/c/lkm4ctr-oppo/extra/devtmpfs/hk/.hk_flush.o.d -nostdinc -I../arch/arm64/include -I./arch/arm64/include/generated -I../include -I./include -I../arch/arm64/include/uapi -I./arch/arm64/include/generated/uapi -I../include/uapi -I./include/generated/uapi -include ../include/linux/compiler-version.h -include ../include/linux/kconfig.h -D__KERNEL__ -mlittle-endian -DKASAN_SHADOW_SCALE_SHIFT= -Qunused-arguments -fmacro-prefix-map=../= -D__ASSEMBLY__ -fno-PIE --target=aarch64-linux-gnu -fintegrated-as -Werror=unknown-warning-option -Werror=ignored-optimization-argument -fasynchronous-unwind-tables -DKASAN_SHADOW_SCALE_SHIFT= -g -gdwarf-4  -DMODULE  -c -o /mnt/c/lkm4ctr-oppo/extra/devtmpfs/hk/hk_flush.o /mnt/c/lkm4ctr-oppo/extra/devtmpfs/hk/hk_flush.S 

source_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/hk/hk_flush.o := /mnt/c/lkm4ctr-oppo/extra/devtmpfs/hk/hk_flush.S

deps_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/hk/hk_flush.o := \
  ../include/linux/compiler-version.h \
    $(wildcard include/config/CC_VERSION_TEXT) \
  ../include/linux/kconfig.h \
    $(wildcard include/config/CPU_BIG_ENDIAN) \
    $(wildcard include/config/BOOGER) \
    $(wildcard include/config/FOO) \

/mnt/c/lkm4ctr-oppo/extra/devtmpfs/hk/hk_flush.o: $(deps_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/hk/hk_flush.o)

$(deps_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/hk/hk_flush.o):
