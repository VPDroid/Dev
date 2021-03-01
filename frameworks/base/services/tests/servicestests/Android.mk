LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# We only want this apk build for tests.
LOCAL_MODULE_TAGS := tests

# Include all test java files.
LOCAL_SRC_FILES := $(call all-java-files-under, src)

LOCAL_STATIC_JAVA_LIBRARIES := \
    services.core \
    services.devicepolicy \
    services.net \
    easymocklib \
    guava \
    mockito-target

LOCAL_JAVA_LIBRARIES := android.test.runner

LOCAL_PACKAGE_NAME := FrameworksServicesTests

LOCAL_CERTIFICATE := platform

include $(BUILD_PACKAGE)

