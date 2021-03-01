# Copyright 2015 The Android Open Source Project
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

ifneq ($(filter angler, $(TARGET_DEVICE)),)

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	voice_processing_descriptors.c

LOCAL_C_INCLUDES += \
	$(call include-path-for, audio-effects)

LOCAL_MODULE := libqcomvoiceprocessingdescriptors

LOCAL_32_BIT_ONLY := true

LOCAL_MODULE_RELATIVE_PATH := soundfx

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif