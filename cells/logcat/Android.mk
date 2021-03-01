# Copyright 2006-2014 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= logcat.cpp event.logtags
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_MODULE := logcat_cells
LOCAL_MODULE_OWNER := cells
LOCAL_CFLAGS := -Werror

include $(BUILD_EXECUTABLE)
