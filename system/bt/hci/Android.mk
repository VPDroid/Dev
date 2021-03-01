LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    src/btsnoop.c \
    src/btsnoop_mem.c \
    src/btsnoop_net.c \
    src/buffer_allocator.c \
    src/hci_audio.c \
    src/hci_hal.c \
    src/hci_hal_h4.c \
    src/hci_hal_mct.c \
    src/hci_inject.c \
    src/hci_layer.c \
    src/hci_packet_factory.c \
    src/hci_packet_parser.c \
    src/low_power_manager.c \
    src/packet_fragmenter.c \
    src/vendor.c

ifeq ($(BLUETOOTH_HCI_USE_MCT),true)
LOCAL_CFLAGS += -DHCI_USE_MCT
endif

LOCAL_CFLAGS += -std=c99 $(bdroid_CFLAGS)

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/include \
    $(LOCAL_PATH)/.. \
    $(LOCAL_PATH)/../include \
    $(LOCAL_PATH)/../btcore/include \
    $(LOCAL_PATH)/../gki/common \
    $(LOCAL_PATH)/../gki/ulinux \
    $(LOCAL_PATH)/../osi/include \
    $(LOCAL_PATH)/../stack/include \
    $(LOCAL_PATH)/../utils/include \
    $(bdroid_C_INCLUDES)

LOCAL_MODULE := libbt-hci
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := STATIC_LIBRARIES

include $(BUILD_STATIC_LIBRARY)

#####################################################
include $(CLEAR_VARS)

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \
    $(LOCAL_PATH)/.. \
    $(LOCAL_PATH)/../include \
    $(LOCAL_PATH)/../btcore/include \
    $(LOCAL_PATH)/../gki/common \
    $(LOCAL_PATH)/../gki/ulinux \
    $(LOCAL_PATH)/../osi/include \
    $(LOCAL_PATH)/../osi/test \
    $(LOCAL_PATH)/../stack/include \
    $(LOCAL_PATH)/../utils/include \
    $(bdroid_C_INCLUDES)


LOCAL_SRC_FILES := \
    ../osi/test/AllocationTestHarness.cpp \
    ../osi/test/AlarmTestHarness.cpp \
    ./test/hci_hal_h4_test.cpp \
    ./test/hci_hal_mct_test.cpp \
    ./test/hci_layer_test.cpp \
    ./test/low_power_manager_test.cpp \
    ./test/packet_fragmenter_test.cpp \
    $(bdroid_C_INCLUDES)


LOCAL_CFLAGS := -Wall -Werror $(bdroid_CFLAGS)
LOCAL_MODULE := net_test_hci
LOCAL_MODULE_TAGS := tests
LOCAL_SHARED_LIBRARIES := liblog libdl
LOCAL_STATIC_LIBRARIES := libbt-hci libosi libcutils libbtcore

include $(BUILD_NATIVE_TEST)
