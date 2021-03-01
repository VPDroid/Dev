# Copyright (C) 2010 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk

LOCAL_MODULE_TAGS := tests
LOCAL_MODULE:= libhwcTest
LOCAL_CFLAGS := -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES
LOCAL_CXX_STL := libc++
LOCAL_SRC_FILES:= hwcTestLib.cpp
LOCAL_C_INCLUDES += system/extras/tests/include \
    $(call include-path-for, opengl-tests-includes) \

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk

LOCAL_MODULE:= hwcStress
LOCAL_MODULE_TAGS := tests
LOCAL_CFLAGS := -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES
LOCAL_CXX_STL := libc++
LOCAL_SRC_FILES:= hwcStress.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libEGL \
    libGLESv2 \
    libutils \
    liblog \
    libui \
    libhardware \

LOCAL_STATIC_LIBRARIES := \
    libtestUtil \
    libglTest \
    libhwcTest \

LOCAL_C_INCLUDES += \
    system/extras/tests/include \
    hardware/libhardware/include \
    $(call include-path-for, opengl-tests-includes) \

include $(BUILD_NATIVE_TEST)

include $(CLEAR_VARS)
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk

LOCAL_MODULE:= hwcRects
LOCAL_MODULE_TAGS := tests
LOCAL_CFLAGS := -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES
LOCAL_CXX_STL := libc++
LOCAL_SRC_FILES:= hwcRects.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libEGL \
    libGLESv2 \
    libutils \
    liblog \
    libui \
    libhardware \

LOCAL_STATIC_LIBRARIES := \
    libtestUtil \
    libglTest \
    libhwcTest \

LOCAL_C_INCLUDES += \
    system/extras/tests/include \
    hardware/libhardware/include \
    $(call include-path-for, opengl-tests-includes) \

include $(BUILD_NATIVE_TEST)

include $(CLEAR_VARS)
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk

LOCAL_MODULE:= hwcColorEquiv
LOCAL_MODULE_TAGS := tests
LOCAL_CFLAGS := -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES
LOCAL_CXX_STL := libc++
LOCAL_SRC_FILES:= hwcColorEquiv.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libEGL \
    libGLESv2 \
    libutils \
    liblog \
    libui \
    libhardware \

LOCAL_STATIC_LIBRARIES := \
    libtestUtil \
    libglTest \
    libhwcTest \

LOCAL_C_INCLUDES += \
    system/extras/tests/include \
    hardware/libhardware/include \
    $(call include-path-for, opengl-tests-includes) \

include $(BUILD_NATIVE_TEST)

include $(CLEAR_VARS)
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk

LOCAL_MODULE:= hwcCommit
LOCAL_MODULE_TAGS := tests
LOCAL_CFLAGS := -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES
LOCAL_CXX_STL := libc++
LOCAL_SRC_FILES:= hwcCommit.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libEGL \
    libGLESv2 \
    libutils \
    liblog \
    libui \
    libhardware \

LOCAL_STATIC_LIBRARIES := \
    libtestUtil \
    libglTest \
    libhwcTest \

LOCAL_C_INCLUDES += \
    system/extras/tests/include \
    hardware/libhardware/include \
    $(call include-path-for, opengl-tests-includes) \

include $(BUILD_NATIVE_TEST)
