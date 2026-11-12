# 实测状态

> 内核 `6.1.157-android14-11-o-g2d576b643492` / KMI `android14-6.1`
> KCFI 严格模式 (`CONFIG_CFI_CLANG=y`, `CFI_PERMISSIVE` 未开)
> 记录时间：2026-11-12

## 一、模块加载

```
$ lsmod | grep lkm4ctr
lkm4ctr               724992  580
```

开机自启由 `/data/adb/modules/lkm4ctr_oppo/service.sh` 完成，
日志 `/data/local/tmp/lkm4ctr.log`：

```
[OK] insmod 成功
[OK] PID namespace 可用
[OK] IPC namespace 可用
```

**关键结论**：`vermagic` 是 `6.1.166-dirty`，设备是 `6.1.157-android14-11`，
但 `insmod` 仍然成功。原因是 `.gnu.linkonce.this_module` 为 **1088 字节**，
与设备上所有厂商 ko 一致——**`struct module` 布局比 vermagic 字符串更有决定性**。

## 二、Droidspaces 检查结果

`droidspaces check` (v7.2.0)：

| 分区 | 结果 |
|---|---|
| MUST HAVE | **10/10 全通过** |
| RECOMMENDED | 9/10（缺 devtmpfs）|
| OPTIONAL | 8/14 |

缺的 6 项：

| 项 | 原因 | 影响 |
|---|---|---|
| `DEVTMPFS` | 未编入内核 | 硬件访问模式回退 tmpfs，节点仍齐全 |
| `MACVLAN` | 未编入内核 | 已用 6.1 原生源码补 `macvlan.ko` |
| `IP6_NF_NAT` | 未编入内核 | 已补 `ip6table_nat.ko` |
| `CFS_BANDWIDTH` | 改动 ABI，无补丁可覆盖 | `--cpus` 被跳过 |
| `CGROUP_PIDS` | 改动 ABI | `--pids-limit` 被跳过 |
| `USER_NS` | 半实现 | `--allow-sandboxing` 不可用 |

## 三、容器实测

用 CLI 创建 Alpine 容器，成功启动：

```
$ droidspaces --name=testalpine --rootfs=... --net=host --init=/bin/sh start
Container started with PID 23319
Container: testalpine (RUNNING)

$ droidspaces --name=testalpine run -- /bin/sh -c "..."
  hostname: testalpine      <- 独立 UTS namespace
  pid: 2                    <- 独立 PID namespace
  uid: 0                    <- 容器内 root
  os: 3.20.3                <- Alpine 真实运行
  /dev: 节点齐全
  apk:  可用
```

## 四、macvlan 的关键教训

pmb110 的 `net_macvlan` 针对 6.12，在 6.1 上编译失败，错误全部来自
6.8+ 才引入的 API 退化：

```
no member named 'lltx' in 'struct net_device'
no member named 'change_proto_down' in 'struct net_device'
field designator 'ndo_hwtstamp_get' does not refer to any field
use of undeclared identifier 'IFLA_MACVLAN_BC_CUTOFF'
call to undeclared function 'generic_hwtstamp_get_lower'
```

**正确做法**：直接用设备内核树自带的 `drivers/net/macvlan.c` 编译，
零修改通过。见 `extra/net_macvlan_61/`。

## 五、为什么 USER_NS 与 pmb110 结论相反

pmb110 (一加 6.12) 报告 `unshare -U` 返回 EINVAL，认定补不了。
本设备实测：

```
unshare -U true   ->  rc=0
```

lkm4ctr 的 `vendor/kernel/user_namespace.c`（1867 行）确实建立了 userns 对象
（diagfs 的 `userns_live` 有值），但：

- `/proc/self/ns/user` 仍不出现
- `uid_map` 为空，写入返回 1
- userns 内 `id -u` 得到 65534 (nobody)

**结论**：syscall 层可用，映射层不完整，属于半实现。
