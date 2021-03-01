LOCAL_PATH:= $(call my-dir)

###############################################################################
# Build META EGL library
#

egl.cfg_config_module :=
# OpenGL drivers config file
ifneq ($(BOARD_EGL_CFG),)

include $(CLEAR_VARS)
LOCAL_MODULE := egl.cfg
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/egl
LOCAL_SRC_FILES := ../../../../$(BOARD_EGL_CFG)
include $(BUILD_PREBUILT)
egl.cfg_config_module := $(LOCAL_MODULE)
endif

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= 	       \
	EGL/egl_tls.cpp        \
	EGL/egl_cache.cpp      \
	EGL/egl_display.cpp    \
	EGL/egl_object.cpp     \
	EGL/egl.cpp 	       \
	EGL/eglApi.cpp 	       \
	EGL/trace.cpp              \
	EGL/getProcAddress.cpp.arm \
	EGL/Loader.cpp 	       \
#

LOCAL_SHARED_LIBRARIES += libcutils libutils liblog libGLES_trace
LOCAL_MODULE:= libEGL
LOCAL_LDFLAGS += -Wl,--exclude-libs=ALL
LOCAL_SHARED_LIBRARIES += libdl
# we need to access the private Bionic header <bionic_tls.h>
LOCAL_C_INCLUDES += bionic/libc/private

LOCAL_CFLAGS += -DLOG_TAG=\"libEGL\"
LOCAL_CFLAGS += -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES
LOCAL_CFLAGS += -fvisibility=hidden
LOCAL_CFLAGS += -DEGL_TRACE=1

ifeq ($(BOARD_ALLOW_EGL_HIBERNATION),true)
  LOCAL_CFLAGS += -DBOARD_ALLOW_EGL_HIBERNATION
endif
ifneq ($(MAX_EGL_CACHE_ENTRY_SIZE),)
  LOCAL_CFLAGS += -DMAX_EGL_CACHE_ENTRY_SIZE=$(MAX_EGL_CACHE_ENTRY_SIZE)
endif

ifneq ($(MAX_EGL_CACHE_KEY_SIZE),)
  LOCAL_CFLAGS += -DMAX_EGL_CACHE_KEY_SIZE=$(MAX_EGL_CACHE_KEY_SIZE)
endif

ifneq ($(MAX_EGL_CACHE_SIZE),)
  LOCAL_CFLAGS += -DMAX_EGL_CACHE_SIZE=$(MAX_EGL_CACHE_SIZE)
endif

LOCAL_REQUIRED_MODULES := $(egl.cfg_config_module)
egl.cfg_config_module :=

include $(BUILD_SHARED_LIBRARY)

###############################################################################
# Build the wrapper OpenGL ES 1.x library
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= 		\
	GLES_CM/gl.cpp.arm 	\
#

LOCAL_CLANG := false
LOCAL_SHARED_LIBRARIES += libcutils liblog libEGL
LOCAL_MODULE:= libGLESv1_CM

LOCAL_SHARED_LIBRARIES += libdl
# we need to access the private Bionic header <bionic_tls.h>
LOCAL_C_INCLUDES += bionic/libc/private

LOCAL_CFLAGS += -DLOG_TAG=\"libGLESv1\"
LOCAL_CFLAGS += -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES
LOCAL_CFLAGS += -fvisibility=hidden

# TODO: This is to work around b/20093774. Remove after root cause is fixed
LOCAL_LDFLAGS_arm += -Wl,--hash-style,both

include $(BUILD_SHARED_LIBRARY)


###############################################################################
# Build the wrapper OpenGL ES 2.x library
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= 		\
	GLES2/gl2.cpp.arm 	\
#

LOCAL_CLANG := false
LOCAL_SHARED_LIBRARIES += libcutils libutils liblog libEGL
LOCAL_MODULE:= libGLESv2

LOCAL_SHARED_LIBRARIES += libdl
# we need to access the private Bionic header <bionic_tls.h>
LOCAL_C_INCLUDES += bionic/libc/private

LOCAL_CFLAGS += -DLOG_TAG=\"libGLESv2\"
LOCAL_CFLAGS += -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES
LOCAL_CFLAGS += -fvisibility=hidden

# TODO: This is to work around b/20093774. Remove after root cause is fixed
LOCAL_LDFLAGS_arm += -Wl,--hash-style,both

# Symlink libGLESv3.so -> libGLESv2.so
# Platform modules should link against libGLESv2.so (-lGLESv2), but NDK apps
# will be linked against libGLESv3.so.
# Note we defer the evaluation of the LOCAL_POST_INSTALL_CMD,
# so $(LOCAL_INSTALLED_MODULE) will be expanded to correct value,
# even for both 32-bit and 64-bit installed files in multilib build.
LOCAL_POST_INSTALL_CMD = \
    $(hide) ln -sf $(notdir $(LOCAL_INSTALLED_MODULE)) $(dir $(LOCAL_INSTALLED_MODULE))libGLESv3.so

include $(BUILD_SHARED_LIBRARY)

###############################################################################
# Build the ETC1 host static library
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= 		\
	ETC1/etc1.cpp 	\
#

LOCAL_MODULE:= libETC1

include $(BUILD_HOST_STATIC_LIBRARY)

###############################################################################
# Build the ETC1 device library
#

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= 		\
	ETC1/etc1.cpp 	\
#

LOCAL_MODULE:= libETC1

include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under,$(LOCAL_PATH))
