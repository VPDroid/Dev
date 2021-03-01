# Copyright 2011, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH:= $(call my-dir)

#Include res dir from photoviewer
photo_dir := ../res ../activity/res
res_dirs := $(photo_dir) res

##################################################
# Build APK
include $(CLEAR_VARS)

src_dirs := src
LOCAL_PACKAGE_NAME := PhotoViewerSample

LOCAL_STATIC_JAVA_LIBRARIES += libphotoviewer

LOCAL_SDK_VERSION := current

LOCAL_SRC_FILES := $(call all-java-files-under, $(src_dirs)) \
        $(call all-logtags-files-under, $(src_dirs))
LOCAL_RESOURCE_DIR := $(addprefix $(LOCAL_PATH)/, $(res_dirs))
LOCAL_AAPT_FLAGS := --auto-add-overlay
LOCAL_AAPT_FLAGS += --extra-packages com.android.ex.photo

include $(BUILD_PACKAGE)

##################################################

#Include res dir from photoviewer
photo_dir := ../res ../appcompat/res
res_dirs := $(photo_dir) res ../../../../$(SUPPORT_LIBRARY_ROOT)/v7/appcompat/res

# Build APK
include $(CLEAR_VARS)

src_dirs := src
LOCAL_PACKAGE_NAME := AppcompatPhotoViewerSample

LOCAL_STATIC_JAVA_LIBRARIES += libphotoviewer_appcompat

LOCAL_SDK_VERSION := current

LOCAL_SRC_FILES := $(call all-java-files-under, $(src_dirs)) \
        $(call all-logtags-files-under, $(src_dirs))
LOCAL_RESOURCE_DIR := $(addprefix $(LOCAL_PATH)/, $(res_dirs))
LOCAL_AAPT_FLAGS := --auto-add-overlay
LOCAL_AAPT_FLAGS += --extra-packages android.support.v7.appcompat:com.android.ex.photo

include $(BUILD_PACKAGE)

##################################################
# Build all sub-directories

include $(call all-makefiles-under,$(LOCAL_PATH))
