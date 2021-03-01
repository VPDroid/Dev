LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/include \
	$(LOCAL_PATH)/../btcore/include \
	$(LOCAL_PATH)/../osi/include \
	$(LOCAL_PATH)/../stack/include \
	$(LOCAL_PATH)/../ \
	$(bdroid_C_INCLUDES)

LOCAL_CFLAGS += $(bdroid_CFLAGS) -std=c99

LOCAL_SRC_FILES := \
	./src/bt_utils.c

LOCAL_MODULE := libbt-utils
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

include $(BUILD_STATIC_LIBRARY)
