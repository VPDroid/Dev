LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:=                     \
        FrameDropper.cpp              \
        GraphicBufferSource.cpp       \
        OMX.cpp                       \
        OMXMaster.cpp                 \
        OMXNodeInstance.cpp           \
        SimpleSoftOMXComponent.cpp    \
        SoftOMXComponent.cpp          \
        SoftOMXPlugin.cpp             \
        SoftVideoDecoderOMXComponent.cpp \
        SoftVideoEncoderOMXComponent.cpp \

LOCAL_C_INCLUDES += \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/frameworks/native/include/media/hardware \
        $(TOP)/frameworks/native/include/media/openmax

LOCAL_SHARED_LIBRARIES :=               \
        libbinder                       \
        libhardware                     \
        libmedia                        \
        libutils                        \
        liblog                          \
        libui                           \
        libgui                          \
        libcutils                       \
        libstagefright_foundation       \
        libdl

LOCAL_MODULE:= libstagefright_omx
LOCAL_CFLAGS += -Werror -Wall
LOCAL_CLANG := true

include $(BUILD_SHARED_LIBRARY)

################################################################################

include $(call all-makefiles-under,$(LOCAL_PATH))
