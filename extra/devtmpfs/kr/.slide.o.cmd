cmd_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/slide.o := clang -Wp,-MMD,/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/.slide.o.d -nostdinc -I../arch/arm64/include -I./arch/arm64/include/generated -I../include -I./include -I../arch/arm64/include/uapi -I./arch/arm64/include/generated/uapi -I../include/uapi -I./include/generated/uapi -include ../include/linux/compiler-version.h -include ../include/linux/kconfig.h -include ../include/linux/compiler_types.h -D__KERNEL__ -mlittle-endian -DKASAN_SHADOW_SCALE_SHIFT= -Qunused-arguments -fmacro-prefix-map=../= -Wall -Wundef -Werror=strict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -fshort-wchar -fno-PIE -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type -Wno-format-security -std=gnu11 --target=aarch64-linux-gnu -fintegrated-as -Werror=unknown-warning-option -Werror=ignored-optimization-argument -mgeneral-regs-only -DCONFIG_CC_HAS_K_CONSTRAINT=1 -Wno-psabi -fasynchronous-unwind-tables -mbranch-protection=pac-ret -Wa,-march=armv8.5-a -DARM64_ASM_ARCH='"armv8.5-a"' -ffixed-x18 -DKASAN_SHADOW_SCALE_SHIFT= -fno-delete-null-pointer-checks -Wno-frame-address -Wno-format-truncation -Wno-format-overflow -Wno-address-of-packed-member -O2 -Wframe-larger-than=6000 -fstack-protector-strong -Wno-gnu -Wno-unused-but-set-variable -Wno-unused-const-variable -fno-omit-frame-pointer -fno-optimize-sibling-calls -ftrivial-auto-var-init=zero -fno-stack-clash-protection -fsanitize=kcfi -Wvla -Wno-pointer-sign -Wcast-function-type -Werror -Wimplicit-fallthrough -fno-strict-overflow -fno-stack-check -Werror=date-time -Werror=incompatible-pointer-types -Wno-initializer-overrides -Wno-sign-compare -Wno-pointer-to-enum-cast -Wno-tautological-constant-out-of-range-compare -Wno-unaligned-access -Wno-cast-function-type-strict -Wno-enum-compare-conditional -g -gdwarf-4 -DOPLUS_FEATURE_CHG_BASIC -mstack-protector-guard=sysreg -mstack-protector-guard-reg=sp_el0 -mstack-protector-guard-offset=1592 -I/mnt/c/lkm4ctr-oppo/extra/devtmpfs -I/mnt/c/lkm4ctr-oppo/extra/devtmpfs/hk -I/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr -Wno-declaration-after-statement -Wno-unused-variable -Wno-unused-function -Wno-strict-prototypes -Wno-missing-prototypes -Wno-incompatible-function-pointer-types  -fsanitize=array-bounds -fsanitize=local-bounds -fsanitize-undefined-trap-on-error  -DMODULE  -DKBUILD_BASENAME='"slide"' -DKBUILD_MODNAME='"devtmpfs_lkm"' -D__KBUILD_MODNAME=kmod_devtmpfs_lkm -c -o /mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/slide.o /mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/slide.c  

source_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/slide.o := /mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/slide.c

deps_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/slide.o := \
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
  ../include/linux/types.h \
    $(wildcard include/config/HAVE_UID16) \
    $(wildcard include/config/UID16) \
    $(wildcard include/config/ARCH_DMA_ADDR_T_64BIT) \
    $(wildcard include/config/PHYS_ADDR_T_64BIT) \
    $(wildcard include/config/64BIT) \
    $(wildcard include/config/ARCH_32BIT_USTAT_F_TINODE) \
  ../include/uapi/linux/types.h \
  arch/arm64/include/generated/uapi/asm/types.h \
  ../include/uapi/asm-generic/types.h \
  ../include/asm-generic/int-ll64.h \
  ../include/uapi/asm-generic/int-ll64.h \
  ../arch/arm64/include/uapi/asm/bitsperlong.h \
  ../include/asm-generic/bitsperlong.h \
  ../include/uapi/asm-generic/bitsperlong.h \
  ../include/uapi/linux/posix_types.h \
  ../include/linux/stddef.h \
  ../include/uapi/linux/stddef.h \
  ../arch/arm64/include/uapi/asm/posix_types.h \
  ../include/uapi/asm-generic/posix_types.h \
  ../include/linux/printk.h \
    $(wildcard include/config/MTK_PRINTK_DEBUG) \
    $(wildcard include/config/MESSAGE_LOGLEVEL_DEFAULT) \
    $(wildcard include/config/CONSOLE_LOGLEVEL_DEFAULT) \
    $(wildcard include/config/CONSOLE_LOGLEVEL_QUIET) \
    $(wildcard include/config/EARLY_PRINTK) \
    $(wildcard include/config/PRINTK) \
    $(wildcard include/config/SMP) \
    $(wildcard include/config/PRINTK_INDEX) \
    $(wildcard include/config/DYNAMIC_DEBUG) \
    $(wildcard include/config/DYNAMIC_DEBUG_CORE) \
  ../include/linux/stdarg.h \
  ../include/linux/init.h \
    $(wildcard include/config/HAVE_ARCH_PREL32_RELOCATIONS) \
    $(wildcard include/config/STRICT_KERNEL_RWX) \
    $(wildcard include/config/STRICT_MODULE_RWX) \
    $(wildcard include/config/LTO_CLANG) \
  ../include/linux/compiler.h \
    $(wildcard include/config/TRACE_BRANCH_PROFILING) \
    $(wildcard include/config/PROFILE_ALL_BRANCHES) \
    $(wildcard include/config/OBJTOOL) \
  ../arch/arm64/include/asm/rwonce.h \
    $(wildcard include/config/LTO) \
    $(wildcard include/config/AS_HAS_LDAPR) \
  ../include/asm-generic/rwonce.h \
  ../include/linux/kasan-checks.h \
    $(wildcard include/config/KASAN_GENERIC) \
    $(wildcard include/config/KASAN_SW_TAGS) \
  ../include/linux/kcsan-checks.h \
    $(wildcard include/config/KCSAN) \
    $(wildcard include/config/KCSAN_WEAK_MEMORY) \
    $(wildcard include/config/KCSAN_IGNORE_ATOMICS) \
  ../include/linux/kern_levels.h \
  ../include/linux/linkage.h \
    $(wildcard include/config/ARCH_USE_SYM_ANNOTATIONS) \
  ../include/linux/stringify.h \
  ../include/linux/export.h \
    $(wildcard include/config/MODVERSIONS) \
    $(wildcard include/config/MODULES) \
    $(wildcard include/config/TRIM_UNUSED_KSYMS) \
  ../arch/arm64/include/asm/linkage.h \
  ../include/linux/ratelimit_types.h \
  ../include/linux/bits.h \
  ../include/linux/const.h \
  ../include/vdso/const.h \
  ../include/uapi/linux/const.h \
  ../include/vdso/bits.h \
  ../include/linux/build_bug.h \
  ../include/uapi/linux/param.h \
  ../arch/arm64/include/uapi/asm/param.h \
  ../include/asm-generic/param.h \
    $(wildcard include/config/HZ) \
  ../include/uapi/asm-generic/param.h \
  ../include/linux/spinlock_types_raw.h \
    $(wildcard include/config/DEBUG_SPINLOCK) \
    $(wildcard include/config/DEBUG_LOCK_ALLOC) \
  ../arch/arm64/include/asm/spinlock_types.h \
  ../include/asm-generic/qspinlock_types.h \
    $(wildcard include/config/NR_CPUS) \
  ../include/asm-generic/qrwlock_types.h \
  ../arch/arm64/include/uapi/asm/byteorder.h \
  ../include/linux/byteorder/little_endian.h \
  ../include/uapi/linux/byteorder/little_endian.h \
  ../include/linux/swab.h \
  ../include/uapi/linux/swab.h \
  arch/arm64/include/generated/uapi/asm/swab.h \
  ../include/uapi/asm-generic/swab.h \
  ../include/linux/byteorder/generic.h \
  ../include/linux/lockdep_types.h \
    $(wildcard include/config/PROVE_RAW_LOCK_NESTING) \
    $(wildcard include/config/LOCKDEP) \
    $(wildcard include/config/LOCK_STAT) \
  ../include/linux/once_lite.h \
  ../include/linux/string.h \
    $(wildcard include/config/BINARY_PRINTF) \
    $(wildcard include/config/FORTIFY_SOURCE) \
  ../include/linux/err.h \
  arch/arm64/include/generated/uapi/asm/errno.h \
  ../include/uapi/asm-generic/errno.h \
  ../include/uapi/asm-generic/errno-base.h \
  ../include/linux/errno.h \
  ../include/uapi/linux/errno.h \
  ../include/linux/overflow.h \
  ../include/linux/limits.h \
  ../include/uapi/linux/limits.h \
  ../include/vdso/limits.h \
  ../include/uapi/linux/string.h \
  ../arch/arm64/include/asm/string.h \
    $(wildcard include/config/ARCH_HAS_UACCESS_FLUSHCACHE) \
  ../include/linux/fortify-string.h \
    $(wildcard include/config/KMSAN) \
  ../include/linux/bug.h \
    $(wildcard include/config/GENERIC_BUG) \
    $(wildcard include/config/BUG_ON_DATA_CORRUPTION) \
  ../arch/arm64/include/asm/bug.h \
  ../arch/arm64/include/asm/asm-bug.h \
    $(wildcard include/config/DEBUG_BUGVERBOSE) \
  ../arch/arm64/include/asm/brk-imm.h \
  ../include/asm-generic/bug.h \
    $(wildcard include/config/BUG) \
    $(wildcard include/config/GENERIC_BUG_RELATIVE_POINTERS) \
  ../include/linux/instrumentation.h \
    $(wildcard include/config/NOINSTR_VALIDATION) \
  ../include/linux/panic.h \
    $(wildcard include/config/PANIC_TIMEOUT) \
  /mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/core.h \
  /mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/slide.h \

/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/slide.o: $(deps_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/slide.o)

$(deps_/mnt/c/lkm4ctr-oppo/extra/devtmpfs/kr/slide.o):
