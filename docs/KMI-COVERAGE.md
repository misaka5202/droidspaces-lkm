# 模块 × KMI 覆盖

每个 KMI 的包从 `droidspaces-lkm-<kmi>-arm64.zip` 下载，
或从全 KMI 合并包 `droidspaces-lkm-arm64.zip` 里按 `/proc/sys/kernel/osrelease`
自动选。

| 模块 | 12-5.10 | 13-5.10 | 13-5.15 | 14-5.15 | 14-6.1 | 15-6.6 | 16-6.12 |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| `lkm4ctr` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `devtmpfs_lkm` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `macvlan` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `vkms` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `ip6table_nat` | ❌ | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |

4 个模块做到了全 KMI 覆盖；`ip6table_nat` 缺两个 5.10。

## 为什么同一个模块在不同 KMI 上会不一样

这些模块有的是**内核树里的普通驱动文件**，API 随版本变化。把某个版本的
源码拿去编另一个版本必然失败，所以构建脚本为每个 KMI 使用**它自己内核树里
的**源码（`/opt/ddk/src/<kmi>`）：

- `vkms` —— 取自 `drivers/gpu/drm/vkms/`，再补 `DRIVER_RENDER`
- `macvlan` —— 取自 `drivers/net/macvlan.c`
- `lkm4ctr` —— 上游自己做了版本适配，直接用
- `devtmpfs_lkm` / `ip6table_nat` —— 本仓库实现，内核里没有可对应的独立驱动

## vkms 的 DRIVER_RENDER 补丁

上游 vkms 的 `driver_features` 是 `MODESET | ATOMIC | GEM`，没有
`DRIVER_RENDER`，因此 `drm_dev_register()` 不会分配 render minor ——
只有一个 card 节点，没有 `/dev/dri/renderD*`。

而 Wayland 合成器要的正是 render node（`gbm_create_device()` +
`drmOpenRender()`）。补上这个标志后同一个模块同时提供两半：

```
crw-rw-rw- root graphics 226,  1  card1        ← KMS 管线
crw-rw-rw- root graphics 226,128  renderD128   ← render node
```

`DRIVER_RENDER` 只允许创建 render minor，不改变 vkms 的 ioctl 集合。

## ip6table_nat 的两个 5.10 缺口

5.10 与 6.1 之间这三个 API 有实质变化，补齐需要为 5.10 写独立实现分支：

```
ip6t_register_table      5.10 期望 5 个参数, 6.1 是 4 个
xt_find_table            参数类型不同
xt_register_template     5.10 头文件里没有对外声明
```

`ip6table_nat` 只负责 IPv6 NAT（Droidspaces 的 `--net=nat` 容器互访），
属于可选能力。缺失时容器仍可正常启动，只是 IPv6 NAT 不可用。

## 已知的构建陷阱

调试过程中踩到的，都已在脚本里处理：

| 现象 | 原因 |
|------|------|
| `Circular <mod>.o <- <mod>.o dependency dropped` | 单文件模块写了 `<mod>-y := <mod>.o`，源码名已等于模块名时 kbuild 会自己推导，多写一行就是自依赖 |
| `'LINUX_VERSION_CODE' is not defined` | 缺 `<linux/version.h>`，`-Werror,-Wundef` 直接失败 |
| `unknown type name 'u8'` | 补声明写进了被 `-include` 强制前置的头文件，那时 `u8` 还没定义 |
| `-Wgcc-compat` 报 `for (int i = ...)` | 老 KMI 的 Makefile 用 `-std=gnu89` + `-Werror` |
| `linux/mnt_idmapping.h: No such file` | 该头 6.3 才引入；compat 层已把 `nop_mnt_idmap` 映射到 `init_user_ns`，include 加版本判断即可 |
| `xwayland` / `kwin` 的 `.deb` 装了但二进制没变 | deb 保留了原时间戳，且 dpkg 对同版本号会跳过；用 `dpkg -i --force-overwrite`，并用 md5 对比 deb 内容确认 |

## 校验方式

CI 的矩阵「全绿」不代表产物齐全：`build_modules.sh` 会记录每个 KMI 的失败项
然后 **exit 0**，好让一个编不过的 KMI 不阻断其他 KMI 出货。判断覆盖度必须看
Release 里实际的 `.ko` 列表，不能看 job 状态。
