# vgem 作为可加载模块
#
# 背景:
#   CONFIG_DRM_VGEM 是编译期选项, 厂商内核没开, 所以没有 /dev/dri/renderD128。
#   而 Hyprland / KWin 的 Wayland 后端硬性要求一个可读写的 DRM render node
#   (hyprland-anland 的检查: [[ -r "$ANLAND_DRM_DEVICE" ]] || die)。
#
#   vgem 是内核自带的"虚拟 GEM"驱动: 它不碰任何硬件, 只提供一个 DRM 设备,
#   支持 GEM 缓冲分配和 dma-buf 导出。这正是 wayland 合成器需要的:
#     - drmOpenRender() 拿到 fd
#     - gbm_create_device() 基于它建 GBM 设备
#     - gbm_bo_create() + dmabuf 导出给 anland
#
#   核心 DRM 符号 (drm_dev_alloc / drm_dev_register /
#   drm_gem_prime_handle_to_fd) 在内核里都是 EXPORT_SYMBOL, 模块可以直接调用。
#
# 源码直接来自内核树 drivers/gpu/drm/vgem/, 未做修改。

obj-m := vgem.o
vgem-y := vgem_drv.o vgem_fence.o

# 内核源码里这组文件是按 CONFIG_DRM_VGEM=y 编译的, 这里强制打开相关的
# 条件编译分支, 让行为与内核原生一致。
ccflags-y += -DCONFIG_DRM_VGEM=1
