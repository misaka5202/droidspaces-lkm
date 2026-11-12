cmd_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/fastboot.o := clang -Wp,-MMD,/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/.fastboot.o.d -nostdinc -I../arch/arm64/include -I./arch/arm64/include/generated -I../include -I./include -I../arch/arm64/include/uapi -I./arch/arm64/include/generated/uapi -I../include/uapi -I./include/generated/uapi -include ../include/linux/compiler-version.h -include ../include/linux/kconfig.h -include ../include/linux/compiler_types.h -D__KERNEL__ -mlittle-endian -DKASAN_SHADOW_SCALE_SHIFT= -Qunused-arguments -fmacro-prefix-map=../= -Wall -Wundef -Werror=strict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -fshort-wchar -fno-PIE -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type -Wno-format-security -std=gnu11 --target=aarch64-linux-gnu -fintegrated-as -Werror=unknown-warning-option -Werror=ignored-optimization-argument -mgeneral-regs-only -DCONFIG_CC_HAS_K_CONSTRAINT=1 -Wno-psabi -fasynchronous-unwind-tables -mbranch-protection=pac-ret -Wa,-march=armv8.5-a -DARM64_ASM_ARCH='"armv8.5-a"' -ffixed-x18 -DKASAN_SHADOW_SCALE_SHIFT= -fno-delete-null-pointer-checks -Wno-frame-address -Wno-format-truncation -Wno-format-overflow -Wno-address-of-packed-member -O2 -Wframe-larger-than=6000 -fstack-protector-strong -Wno-gnu -Wno-unused-but-set-variable -Wno-unused-const-variable -fno-omit-frame-pointer -fno-optimize-sibling-calls -ftrivial-auto-var-init=zero -fno-stack-clash-protection -fsanitize=kcfi -Wvla -Wno-pointer-sign -Wcast-function-type -Werror -Wimplicit-fallthrough -fno-strict-overflow -fno-stack-check -Werror=date-time -Werror=incompatible-pointer-types -Wno-initializer-overrides -Wno-sign-compare -Wno-pointer-to-enum-cast -Wno-tautological-constant-out-of-range-compare -Wno-unaligned-access -Wno-cast-function-type-strict -Wno-enum-compare-conditional -g -gdwarf-4 -DOPLUS_FEATURE_CHG_BASIC -mstack-protector-guard=sysreg -mstack-protector-guard-reg=sp_el0 -mstack-protector-guard-offset=1592 -I/mnt/c/lkm4ctr-oppo/extra/devtmpfs -I/mnt/c/lkm4ctr-oppo/extra/devtmpfs/hk -I/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr -Wno-declaration-after-statement -Wno-unused-variable -Wno-unused-function -Wno-strict-prototypes -Wno-missing-prototypes -Wno-incompatible-function-pointer-types  -fsanitize=array-bounds -fsanitize=local-bounds -fsanitize-undefined-trap-on-error  -DMODULE  -DKBUILD_BASENAME='"fastboot"' -DKBUILD_MODNAME='"devtmpfs_lkm"' -D__KBUILD_MODNAME=kmod_devtmpfs_lkm -c -o /mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/fastboot.o /mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/fastboot.c  

source_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/fastboot.o := /mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/fastboot.c

deps_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/fastboot.o := \
  ../include/linux/compiler-version.h \
    $(wildcard include/config/CC_VERSION_TEXT) \
  ../include/linux/kconfig.h \
    $(wildcard include/config/CPU_BIG_ENDIAN) \
    $(wildcard include/config/BOOGER) \
    $(wildcard include/config/FOO) \
  ../include/linux/compiler_types.h \
    $(wildcard include/config/DEBUG_INFO_BTF) \
    $(wildcard include/config/PAHOLE_HAS_BTF_TAG) \
    $(wildcard include/config/HAVE_ARCH_COMPILER_H) \
    $(wildcard include/config/CC_HAS_ASM_INLINE) \
  ../include/linux/compiler_attributes.h \
  ../include/linux/compiler-clang.h \
    $(wildcard include/config/ARCH_USE_BUILTIN_BSWAP) \
    $(wildcard include/config/CLANG_VERSION) \
  ../arch/arm64/include/asm/compiler.h \

/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/fastboot.o: $(deps_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/fastboot.o)

$(deps_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/fastboot.o):
