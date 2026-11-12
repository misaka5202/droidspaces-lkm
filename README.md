# lkm4ctr for OPPO Find X7 (MT6989)

验证 [cyanmint/lkm4ctr](https://github.com/cyanmint/lkm4ctr) 能否在 OPPO Find X7
(`6.1.157-android14-11-o-g2d576b643492`, KMI `android14-6.1`) 上编译并加载。

## 目标

在**不刷内核**的前提下，用 LKM 补齐容器运行时缺失的内核能力：

| CONFIG | 设备现状 | lkm4ctr 能否补 |
|--------|---------|:---:|
| `CONFIG_PID_NS` | not set | 声称能 |
| `CONFIG_SYSVIPC` | not set | 声称能 |
| `CONFIG_DEVTMPFS` | not set | 声称能 |
| `CONFIG_CGROUP_PIDS` | not set | 声称能 |
| `CONFIG_USER_NS` | not set | **待验证** |
| `CONFIG_IPC_NS` | =y | 已有 |
| `CONFIG_POSIX_MQUEUE` | =y | 已有 |
| `CONFIG_OVERLAY_FS` | =y | 已有 |
| `CONFIG_KPROBES` | =y | hook 后端依赖 |

## 关键疑点

lkm4ctr 的 `vendor/kernel/user_namespace.c` 有 1867 行，实现了
`vns_create_user_ns()`、`vns_make_kuid()`、`vns_map_id_range_down()`，
声称能在 `CONFIG_USER_NS=n` 的内核上提供真正的 user namespace。

而另一项目 [ai-luo2006/droidspaces-6.12-pmb110](https://github.com/ai-luo2006/droidspaces-6.12-pmb110)
在同一目标上明确结论是「USER_NS 补不了，容器 root 就是宿主 root」。

两者结论冲突，本仓库用来实测验证。

## 构建

用 SukiSU-Ultra 的 DDK 容器
(`ghcr.io/ylarod/ddk-min:android14-6.1-20260828`)，内含该 KMI 真实的
`vmlinux` + `Module.symvers` + 匹配的 clang 工具链。

```bash
make -C /opt/ddk/kdir/android14-6.1 M=$PWD/lkm4ctr/vendor modules
```
