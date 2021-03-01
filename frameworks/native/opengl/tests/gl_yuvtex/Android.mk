LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	gl_yuvtex.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libEGL \
    libGLESv1_CM \
    libutils \
    libui \
    libgui

LOCAL_STATIC_LIBRARIES += libglTest

LOCAL_C_INCLUDES += $(call include-path-for, opengl-tests-includes)

LOCAL_MODULE:= test-opengl-gl_yuvtex

LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS := -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES

include $(BUILD_EXECUTABLE)
