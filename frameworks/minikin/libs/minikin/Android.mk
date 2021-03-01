# Copyright (C) 2013 The Android Open Source Project
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

minikin_src_files := \
    AnalyzeStyle.cpp \
    CmapCoverage.cpp \
    FontCollection.cpp \
    FontFamily.cpp \
    GraphemeBreak.cpp \
    Hyphenator.cpp \
    Layout.cpp \
    LineBreaker.cpp \
    Measurement.cpp \
    MinikinInternal.cpp \
    MinikinRefCounted.cpp \
    MinikinFontFreeType.cpp \
    SparseBitSet.cpp

minikin_c_includes += \
    external/harfbuzz_ng/src \
    external/freetype/include \
    frameworks/minikin/include

minikin_shared_libraries := \
    libharfbuzz_ng \
    libft2 \
    liblog \
    libpng \
    libz \
    libicuuc \
    libutils

LOCAL_MODULE := libminikin
LOCAL_EXPORT_C_INCLUDE_DIRS := frameworks/minikin/include
LOCAL_SRC_FILES := $(minikin_src_files)
LOCAL_C_INCLUDES := $(minikin_c_includes)
LOCAL_SHARED_LIBRARIES := $(minikin_shared_libraries)

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libminikin
LOCAL_MODULE_TAGS := optional
LOCAL_EXPORT_C_INCLUDE_DIRS := frameworks/minikin/include
LOCAL_SRC_FILES := $(minikin_src_files)
LOCAL_C_INCLUDES := $(minikin_c_includes)
LOCAL_SHARED_LIBRARIES := $(minikin_shared_libraries)

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

# Reduced library (currently just hyphenation) for host

LOCAL_MODULE := libminikin_host
LOCAL_MODULE_TAGS := optional
LOCAL_EXPORT_C_INCLUDE_DIRS := frameworks/minikin/include
LOCAL_C_INCLUDES := $(minikin_c_includes)
LOCAL_SHARED_LIBRARIES := liblog libicuuc-host

LOCAL_SRC_FILES := Hyphenator.cpp

include $(BUILD_HOST_STATIC_LIBRARY)
