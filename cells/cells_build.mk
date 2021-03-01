
##########################################################
# Library for Dalvik-specific functions
##########################################################
ifeq (1,$(strip $(shell expr $(PLATFORM_SDK_VERSION) \< 21)))
PRODUCT_PACKAGES += libxposed_dalvik
endif

##########################################################
# Library for ART-specific functions
##########################################################

PRODUCT_PACKAGES += celld cellc cellsservice cswitch  cellsupdownvm cellssync kernel_kmsg_cells logcat_cells  switchsystem
