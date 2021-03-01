#
#  Copyright (C) 2014 Google, Inc.
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at:
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := net_test_bluedroid

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/../../

LOCAL_SRC_FILES := \
    cases/adapter.c \
    cases/cases.c \
    cases/gatt.c \
    cases/pan.c \
    cases/rfcomm.c \
    support/adapter.c \
    support/callbacks.c \
    support/gatt.c \
    support/hal.c \
    support/pan.c \
    support/rfcomm.c \
    main.c

LOCAL_SHARED_LIBRARIES += \
    liblog \
    libhardware \
    libhardware_legacy \
    libcutils

LOCAL_STATIC_LIBRARIES += \
  libbtcore \
  libosi

LOCAL_CFLAGS += -std=c99 -Wall -Wno-unused-parameter -Wno-missing-field-initializers -Werror

LOCAL_MULTILIB := 32

include $(BUILD_EXECUTABLE)
