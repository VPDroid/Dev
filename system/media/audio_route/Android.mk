LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	external/expat/lib
LOCAL_SRC_FILES:= audio_route.c
LOCAL_MODULE := libaudioroute
LOCAL_SHARED_LIBRARIES:= liblog libcutils libutils libexpat libtinyalsa
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
