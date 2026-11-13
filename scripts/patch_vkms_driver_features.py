#!/usr/bin/env python3
"""给 vkms 的 driver_features 追加 DRIVER_RENDER。

为什么需要这个补丁
------------------
上游 vkms 声明的是 DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM，没有
DRIVER_RENDER。drm_dev_register() 只有在看到 DRIVER_RENDER 时才分配 render
minor，于是未打补丁的 vkms 只创建 card 节点，没有 /dev/dri/renderD<N>。

Wayland 合成器（Hyprland / KWin）走的是 gbm_create_device() + drmOpenRender()，
要的正是 render node，所以没有它就会停在：

    CRIT: Couldn't open a gbm device   (OpenGL.cpp:351)

补上这个标志后同一个模块同时提供两半：
    card1        KMS 管线（CRTC / plane / writeback）
    renderD128   render node

用法: patch_vkms_driver_features.py <vkms_drv.c>
退出码 0 表示文件里已有 DRIVER_RENDER 或修补成功；非 0 表示没有找到
driver_features 字段。
"""

import re
import sys


def main() -> int:
    if len(sys.argv) != 2:
        return 2
    path = sys.argv[1]

    try:
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
    except OSError as exc:
        print(f"patch_vkms: cannot read {path}: {exc}", file=sys.stderr)
        return 2

    if "DRIVER_RENDER" in text:
        return 0

    # 只匹配到逗号为止：值本身不含逗号，逗号是结构体成员的分隔符。
    # 追加必须落在值末尾、逗号之前，写成 "... | GEM, | DRIVER_RENDER,"
    # 是语法错误。
    pattern = re.compile(r"\.driver_features\s*=\s*([^,]+)")

    def repl(match: "re.Match[str]") -> str:
        body = match.group(1).rstrip()
        if "DRIVER_RENDER" in body:
            return match.group(0)
        return f".driver_features = {body} | DRIVER_RENDER"

    new_text, count = pattern.subn(repl, text, count=1)

    if count == 0:
        # 没有这个字段时退回整体替换，至少不要静默失败
        if "DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM" not in text:
            print(
                "patch_vkms: no .driver_features field and no known flag set",
                file=sys.stderr,
            )
            return 1
        new_text = text.replace(
            "DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM",
            "DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM | DRIVER_RENDER",
            1,
        )

    try:
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(new_text)
    except OSError as exc:
        print(f"patch_vkms: cannot write {path}: {exc}", file=sys.stderr)
        return 2

    return 0


if __name__ == "__main__":
    sys.exit(main())
