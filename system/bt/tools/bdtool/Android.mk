#
# Copyright (C) 2014 The Android Open Source Project
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
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := net_bdtool

LOCAL_SRC_FILES := \
  adapter.c \
  bdtool.c \
  ../../test/suite/support/callbacks.c \
  ../../test/suite/support/gatt.c \
  ../../test/suite/support/hal.c \
  ../../test/suite/support/pan.c

LOCAL_STATIC_LIBRARIES := \
  libbtcore \
  libosi

LOCAL_CFLAGS := -std=c99 $(bdroid_CFLAGS) -Wno-unused-parameter -Wno-missing-field-initializers

LOCAL_C_INCLUDES := \
  $(LOCAL_PATH)/../../test/suite \
  $(LOCAL_PATH)/../..

LOCAL_SHARED_LIBRARIES += \
  libhardware liblog

include $(BUILD_EXECUTABLE)
