# VPDroid（Android 10）

We develop a lightweight Android OS-level virtualization architecture, VPDroid, to assist apps’ account security testing. With VPDroid, security analysts are able to configure different device attributes according to a target phone’s profiles and then boot up a virtual phone (VP) environment that closely approximates the target device. To deceive the cloned apps into thinking the smartphone is not changed, VPDroid has to meet two requirements (RQ1 & RQ2):

- RQ1: the VP always gets direct access to hardware devices; this design provides a close-to-native virtual
environment with high performance.

- RQ2: user-mode apps in the VP are imperceptible to the change of device; this requires our virtualization and
device-attribute customization functions are invisible to user-mode apps running in the VP.

VPDroid is built on top of Cells, because its foreground VP design meets RQ1. However, Cells fails to meet RQ2: it is not designed to edit device attributes, and its user-level device virtualization modifies the VP’s application framework layer, which can be detected by VP’s apps. Besides, Cells’s kernel-level device virtualization to many hardware devices are not compatible with Android 6.0 and later versions any more. We improve Cells significantly to achieve our requirements on mainstream Android versions.

# Overview of VPDroid's virtualization architecture.

<img src="https://github.com/VPDroid/Dev/blob/main/VPDroid-Architecture-new.pdf" width="450" />

 
The grey boxes represent Cells's modules reused by VPDroid.

# Code introduction

Since VPDroid is a os-level code with a large amount of code, we will upload the part of the code that belongs to “NEW USER-LEVEL DEVICE VIRTUALIZATION” and “CUSTOMIZE THE VP’S DEVICE ATTRIBUTES.”  The basic source code included in the VPDroid code is the android operation system source code that has not been modified; we will no longer upload it but will provide a download link.

The codes we created are as follows, which are based on the Android Open Source Project (AOSP) (https://android.googlesource.com/platform/manifest) and Cells (https://cells-source.cs.columbia.edu/ ).

## cells
  cells/: VP manager daemons
    
  cellsservice/: VP manager

  cellsapp/: switch applications

  busybox/: Linux tools

## system
  core/adb/: adb mutex virtualization

  core/init/: VP startup

  core/rootdir/init.cells.rc: VP's init.rc

  core/rootdir/cells/: additional configuration files for the VP

  core/sdcard/: sdcard virtualization

## kernel
  drivers/base/core.c: drv namespace initialization

  kernel/drv_namespace.c: drv namespace api

  kernel/nsproxy.c: namespaces proxy

  drivers/android/binder.c: binder virtualization

  drivers/input/evdev.c: input virtualization

  kernel/power/wakelock.c: wakelock virtualization

## frameworks
  av/: camera audio video media virtualization

  native/libs/binder/: binder virtualization

  native/libs/sensor/: sensor virtualization

  native/services/surfaceflinger/: display virtualization

  base/services/core/java/com/android/server/CellsService.java: net virtualization

  base/core/java/android/app/CellsPrivateServiceManager.java: cellsservice java service
## Configuration file

  share-services: Compile system configuration file

  build.VPDroid.prop/ build.VPDroid-1.prop/ build.VPDroid-2.prop/ build.VPDroid-3.prop:  Custom configuration file for update Customized
 environment

# basic code 

The Android source code is:  android-10.0.0_r33

The ways to download source code:  https://source.android.com/setup/develop/repo?hl=zh-cn

# System Prerequisites

Operating System: Ubuntu 20.04 LTS

JDK version：openJDK version 9

# Compile command

source build/envsetup.sh

lunch 

make -j4


# Benchmarks and Samples:

## Benchmarks 

Linpack (v1.1) for CPU; 

Quadrant advanced edition (v2.1.1) for 2D graphics and file I/O; 

3DMark (v2.0.4646) for 3D graphics;

SunSpider (v1.0.2) for web browsing;

BusyBox wget (v1.21.1) for networking

## Samples

Target apps from Google Play store and Huawei/Xiaomi app markets in China. 

The selection criteria are: 1) the app is among the top 300 apps in that market; 2) it has more than 1 million downloads. After that, we have to install each app on a real device to test whether it can work properly. 
