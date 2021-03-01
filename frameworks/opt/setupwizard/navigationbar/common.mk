#
# Include this make file to build your application against this module.
#
# Make sure to include it after you've set all your desired LOCAL variables.
# Note that you must explicitly set your LOCAL_RESOURCE_DIR before including this file.
#
# For example:
#
#   LOCAL_RESOURCE_DIR := \
#        $(LOCAL_PATH)/res
#
#   include frameworks/opt/setupwizard/navigationbar/common.mk
#

LOCAL_RESOURCE_DIR += $(call my-dir)/res
LOCAL_AAPT_FLAGS += --auto-add-overlay --extra-packages com.android.setupwizard.navigationbar
LOCAL_STATIC_JAVA_LIBRARIES += setup-wizard-navbar
