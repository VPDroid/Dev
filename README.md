# Android 10 VPDroid

We develop a lightweight Android OS-level virtualization architecture, VPDroid, to assist apps’ account security testing. With VPDroid, security analysts can configure different device attributes according to a target phone’s profiles and then boot up a virtual phone (VP) environment that closely approximates the target device. To deceive the cloned apps into thinking the smartphone is not changed, VPDroid has to meet two requirements (RQ1 & RQ2):
- RQ1: the VP always gets direct access to hardware devices; this design provides a close-to-native virtual environment with high performance.
- RQ2: user-mode apps in the VP are imperceptible to the change of device; this requires our virtualization and device-attribute customization functions are invisible to user-mode apps running in the VP.

VPDroid is built on top of Cells, because its foreground VP design meets RQ1. However, Cells fails to meet RQ2: it is not designed to edit device attributes, and its user-level device virtualization modifies the VP’s application framework layer, which can be detected by VP’s apps. Besides, Cells’s kernel-level device virtualization to many hardware devices are not compatible with Android 6.0 and later versions any more. We improve Cells significantly to achieve our requirements on mainstream Android versions.

## cells
  cells/: Container Manager Daemons
    
  cellsservice/: Container Manager

  cellsapp/: Switch Applications

  busybox/: Linux tools

## system
  core/adb/: adb Mutex virtualization

  core/init/: Container Startup

  core/rootdir/init.cells.rc: Container's init.rc

  core/rootdir/cells/: Additional configuration files for the container

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

