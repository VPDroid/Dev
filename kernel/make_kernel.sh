#!/bin/sh
make  ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- angler_defconfig
make  ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- -j8
cp arch/arm64/boot/Image.gz-dtb ../device/huawei/angler-kernel/
