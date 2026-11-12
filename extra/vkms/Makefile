# vkms 作为可加载模块
#
# 为什么是 vkms 而不是 vgem:
#   vgem 只提供 GEM 缓冲分配, 没有 KMS (显示管线) 那一套。Mesa 的 GBM 后端
#   (dri_gbm.so) 会按 DRM driver 名字去找对应的 dri 驱动, 而完整安装的 Mesa
#   里有 vkms_dri.so 却没有 vgem_dri.so:
#
#     /usr/lib/aarch64-linux-gnu/dri/  ->  kms_swrast_dri.so  swrast_dri.so
#                                           vkms_dri.so  virtio_gpu_dri.so
#                                           (没有 vgem_dri.so)
#
#   所以 Hyprland 用 vgem 时会停在:
#     DEBUG: openRenderNode got drm device /dev/dri/renderD128
#     CRIT : Couldn't open a gbm device   (OpenGL.cpp:351)
#
#   vkms 是内核自带的"虚拟 KMS"驱动: 它创建一个完整的软件 DRM 设备
#   (card + render node + CRTC + plane), 不接任何真实显示硬件, 配合
#   kms_swrast_dri.so 就能跑起软件渲染的合成器。
#
# 源码直接取自内核树 drivers/gpu/drm/vkms/, 未做修改。

obj-m := vkms.o
vkms-y := vkms_drv.o vkms_plane.o vkms_output.o vkms_formats.o \
          vkms_crtc.o vkms_composer.o vkms_writeback.o

ccflags-y += -DCONFIG_DRM_VKMS=1
