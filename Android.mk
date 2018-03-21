LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := memreport.c
LOCAL_MODULE := memreport
LOCAL_MODULE_TAGS := optional tests
LOCAL_SHARED_LIBRARIES := libutils libcutils
include $(BUILD_EXECUTABLE)
