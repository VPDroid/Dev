# App’s Auto-Login Function Security Testing via Android OS-Level Virtualization
## Paper
This software is the outcome of our accademic research. 

If you use this code, please cite our accademic paper as:
```
@inproceedings{Wen2021A,
  title={App’s Auto-Login Function Security Testing via Android OS-Level Virtualization},
  author={Wenna, Song and Jiang, Ming and Lin, Jiang and Han, Yan and Yi, Xiang and Yuan, Chen and Jianming, Fu and Guojun, Peng},
  booktitle={Proceedings of 43rd International Conference on Software Engineering},
  year={2021}
}
```
## VPDroid 

We now provide is the VPDroid source version of Android 6.0.

We develop a lightweight Android OS-level virtualization architecture, VPDroid, to assist apps’ account security testing. With VPDroid, security analysts are able to configure different device attributes according to a target phone’s profiles and then boot up a virtual phone (VP) environment that closely approximates the target device. To deceive the cloned apps into thinking the smartphone is not changed, VPDroid has to meet two requirements (RQ1 & RQ2):

- RQ1: the VP always gets direct access to hardware devices; this design provides a close-to-native virtual
environment with high performance.

- RQ2: user-mode apps in the VP are imperceptible to the change of device; this requires our virtualization and
device-attribute customization functions are invisible to user-mode apps running in the VP.

VPDroid is built on top of Cells, because its foreground VP design meets RQ1. However, Cells fails to meet RQ2: it is not designed to edit device attributes, and its user-level device virtualization modifies the VP’s application framework layer, which can be detected by VP’s apps. Besides, Cells’s kernel-level device virtualization to many hardware devices are not compatible with Android 6.0 and later versions any more. We improve Cells significantly to achieve our requirements on mainstream Android versions.

Our clone attack demo video (https://youtu.be/cs6LxbDGPXU) shows that VPDroid enables the attacker to bypass KakaoTalk’s device-consistency check, and the victim is unaware that her account has been compromised. 

## Overview of VPDroid's Virtualization Architecture


<img src="https://github.com/VPDroid/Dev/blob/main/VPDroid-Architecture-new1.png" width="780">
 
The grey boxes represent Cells's modules reused by VPDroid. The white boxes represent functional modules updated by VPDroid. For updated code implementation, please see the section of the code introduction below. 

## Code Introduction

Since VPDroid is an os-level code with a vast amount of code, we will upload the part of the code that belongs to “NEW USER-LEVEL DEVICE VIRTUALIZATION,” some kernel virtualization, and “CUSTOMIZE THE VP’S DEVICE ATTRIBUTES.” The basic code included in the VPDroid is from the operation system source code of android that we have not modified; we will no longer upload it but will provide a download link.

We have created the VPDroid code as follows, which are based on the Android Open Source Project (AOSP) (https://android.googlesource.com/platform/manifest) and Cells (https://cells-source.cs.columbia.edu/ ). 

### cells
  cells/: VP manager daemons
    
  cellsservice/: VP manager

  cellsapp/: switch applications

  rilproxy/: rild virtualization

  qmuxproxy/: QCOM gps audio radio bluetooth socket data proxy

  busybox/: Linux tools

### system
  core/adb/: adb mutex virtualization

  core/init/: VP startup

  core/rootdir/init.cells.rc: VP's init.rc

  core/rootdir/cells/: additional configuration files for the VP

  core/sdcard/: sdcard virtualization

### kernel
  drivers/base/core.c: drv namespace initialization

  kernel/drv_namespace.c: drv namespace api

  kernel/nsproxy.c: namespaces proxy

  drivers/android/binder.c: binder virtualization

  drivers/input/evdev.c: input virtualization

  kernel/power/wakelock.c: wakelock virtualization

### frameworks
  av/: camera audio video media virtualization

  native/libs/binder/: binder virtualization

  native/services/surfaceflinger/: display virtualization

  base/services/core/java/com/android/server/CellsService.java: net virtualization

  base/core/java/android/app/CellsPrivateServiceManager.java: cellsservice java service
  
### android-binder
  binder virtualization  of the android operating system

### configuration file

  share-services: compile system configuration file
  
  kernel-modify-config： kernel config modify file

  build.VPDroid.prop/:  custom configuration file for update customized environment
 
### basic code 

The Android source code is:  android-6.0.1_r62

The ways to download source code:  https://source.android.com/setup/develop/repo

## Compilation system requirements

VMWare Operating System: Ubuntu 20.04 LTS

JDK version：openJDK version 7

## Compile Command

`source build/envsetup.sh`

`lunch 17`

`make -j4`


## Benchmarks and Samples

### Benchmarks 

Linpack (v1.1) for CPU; 

Quadrant advanced edition (v2.1.1) for 2D graphics and file I/O; 

3DMark (v2.0.4646) for 3D graphics;

SunSpider (v1.0.2) for web browsing;

BusyBox wget (v1.21.1) for networking.

### Samples

Target apps from Google Play store and Huawei/Xiaomi app markets in China. 

The selection criteria are: 1) the app is among the top 300 apps in that market; 2) it has more than 1 million downloads. After that, we have to install each app on a real device to test whether it can work properly. 
