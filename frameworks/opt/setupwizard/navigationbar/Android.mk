LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SDK_VERSION := current
LOCAL_MODULE := setup-wizard-navbar
LOCAL_RESOURCE_DIR := $(LOCAL_PATH)/res
LOCAL_SRC_FILES := $(call all-java-files-under, src)

include $(BUILD_STATIC_JAVA_LIBRARY)
