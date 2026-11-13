# 模块 × KMI 覆盖

**5 个模块 × 7 个 KMI = 35/35，全部齐全。**

| 模块 | 12-5.10 | 13-5.10 | 13-5.15 | 14-5.15 | 14-6.1 | 15-6.6 | 16-6.12 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| `lkm4ctr` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `devtmpfs_lkm` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `macvlan` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `ip6table_nat` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `vkms` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

下载全 KMI 合并包 `droidspaces-lkm-arm64.zip`（开机按 `osrelease` 自动选），
或按 KMI 精确下载 `droidspaces-lkm-<kmi>-arm64.zip`。

## 构建方式

每个 KMI 用**它自己内核树里的**驱动源码（DDK 镜像的 `/opt/ddk/src/<kmi>`），
不是跨版本共用一份副本：

| 模块 | 源码来源 |
|------|---------|
| `vkms` | `drivers/gpu/drm/vkms/` + `DRIVER_RENDER` 补丁 |
| `macvlan` | `drivers/net/macvlan.c` |
| `lkm4ctr` | 上游仓库（已自带版本适配） |
| `devtmpfs_lkm` | 本仓库实现 |
| `ip6table_nat` | 本仓库实现（含 5.10/5.15/6.1+ 三套签名分支） |

## vkms 的 DRIVER_RENDER 补丁

上游 vkms 的 `driver_features` 是 `MODESET | ATOMIC | GEM`，没有
`DRIVER_RENDER`。而 `drm_dev_register()` 只有看到这个标志才分配 render
minor —— 于是未打补丁的 vkms 只创建 card，没有 `/dev/dri/renderD<N>`：

```
crw-rw-rw- 226,   1  card1          ← 有
                                     ← renderD128 没有
```

Wayland 合成器走 `gbm_create_device()` + `drmOpenRender()`，要的正是 render
node，缺它就会停在：

```
CRIT: Couldn't open a gbm device   (OpenGL.cpp:351)
```

补上后同一个模块同时提供两半：

```
crw-rw----  system system   226,   0  card0        (厂商显示控制器)
crw-rw-rw-  root   graphics 226,   1  card1        ← vkms KMS 管线
crw-rw-rw-  root   graphics 226, 128  renderD128   ← vkms render node
```

`DRIVER_RENDER` 只允许创建 render minor，不改变 vkms 的 ioctl 集合。

补丁由 `scripts/patch_vkms_driver_features.py` 完成，`build_modules.sh` 在
编译前会**二次校验**文件里确实有 `DRIVER_RENDER`，没有就不出货。

## 5.10 与 5.15+ 的 API 差异

`ip6table_nat` 需要三套分支，分界线在两个位置：

| API | 5.10 | 5.15+ | 6.1+ |
|-----|:---:|:---:|:---:|
| `ip6t_do_table` | `(skb,state,table)` | 同 5.10 | `(priv,skb,state)` |
| `ip6t_register_table` | 5 参数（+`**res`） | 4 参数 | 4 参数 |
| `ip6t_unregister_table_exit` | `(net,table)` | `(net,name)` | `(net,name)` |
| `xt_register_template` | **不存在** | 有 | 有 |
| `xt_find_table` | **不存在** | 有 | 有 |

5.10 上表存在 `net->ipv6.ip6table_nat`，注册时直接从那个字段取回，不需要
`xt_find_table`；module_init 也只对 `init_net` 建一次表，不走 template 机制。

## 构建陷阱

调试过程中踩到的，都已在脚本里处理：

| 现象 | 原因 |
|------|------|
| 全部 KMI「成功」但 vkms 产物 0 个 | `build_modules.sh` 记录失败后 `exit 0`（故意，为了不让一个 KMI 阻断其他），所以**矩阵全绿不代表产物齐全** |
| `vkms` 能加载但无 renderD128 | 补丁用的内联 heredoc 正则被 shell 转义吃掉（`[^,\n]` 变成真实换行），补丁静默失效 |
| `Circular <mod>.o <- <mod>.o dependency dropped` | 单文件模块写了 `<mod>-y := <mod>.o`，源码名等于模块名时 kbuild 自己推导，多写一行就是自依赖 |
| `'LINUX_VERSION_CODE' is not defined` | 缺 `<linux/version.h>`，`-Werror,-Wundef` 直接失败 |
| `unknown type name 'u8'` | 补声明写进了被 `-include` 强制前置的头文件，那时 `u8` 还没定义 |
| `-Wgcc-compat` 报 `for (int i = ...)` | 老 KMI 的 Makefile 用 `-std=gnu89` + `-Werror` |
| `linux/mnt_idmapping.h: No such file` | 该头 6.3 才引入；compat 层已把 `nop_mnt_idmap` 映射到 `init_user_ns`，include 加版本判断即可 |
| `No such file or directory` 但看不到缺什么 | 把 `if_macvlan.h` 复制进模块目录不满足 `#include <linux/if_macvlan.h>`，要从内核 include 路径解析 |

## 判断覆盖度的方法

**不要看 CI job 状态。** 看 Release 里实际的 `.ko` 列表：

```bash
gh release view <tag> --json assets -q '.assets[].name' \
  | grep -E '\.ko$' | sed 's/-android[0-9]*-[0-9.]*\.ko$//' | sort | uniq -c
```

或者解压每个 KMI 包数一下：

```bash
for z in droidspaces-lkm-android*.zip; do
  echo "$z: $(unzip -l "$z" | grep -c '\.ko$') 个"
done
```

## 真机验证记录

`android14-6.1`（MTK SoC，GKI 6.1）实测通过：

```
$ insmod vkms-android14-6.1.ko
[drm] Initialized vkms 1.0.0 20180514 for vkms on minor 1

$ ls -la /dev/dri/
crw-rw-rw-  root graphics 226,   1  card1
crw-rw-rw-  root graphics 226, 128  renderD128

$ droidspaces --name=vtest --anland --gpu start
Container: vtest (RUNNING)   HW access: GPU   Anland: enabled
容器内: crw-rw---- root droidspaces-gpu 226, 128 renderD128
```

`ip6table_nat` 加载时自解析符号：

```
[ip6t-nat-shim] self-resolved 9/9 symbols
```
