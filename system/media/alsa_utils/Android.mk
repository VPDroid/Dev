# Copyright (C) 2015 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libalsautils
LOCAL_SRC_FILES := \
	alsa_device_profile.c \
	alsa_device_proxy.c \
	alsa_logging.c \
	alsa_format.c
LOCAL_C_INCLUDES += \
	external/tinyalsa/include
LOCAL_EXPORT_C_INCLUDE_DIRS := system/media/alsa_utils/include
LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa libaudioutils
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Wno-unused-parameter

include $(BUILD_SHARED_LIBRARY)

