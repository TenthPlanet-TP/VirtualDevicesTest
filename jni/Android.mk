
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libsharedbufferclient
LOCAL_SRC_FILES := $(LOCAL_PATH)/../3rd_part/vir_dev_sdk/libs/libsharedbufferclient.sbc.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	../utils.cpp \
	../video_extractor.cpp \
	../main.cpp

#../media_extractor_test.cpp \

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../ \
	$(LOCAL_PATH)/../3rd_part/vir_dev_sdk/inc/ \

# LOCAL_SHARED_LIBRARIES := \
#	libcutils libutils libbinder \
# 	libgui liblog \
# 	libmediandk \

LOCAL_SHARED_LIBRARIES := \
	libsharedbufferclient \

LOCAL_LDLIBS := -llog -lmediandk

# LOCAL_VENDOR_MODULE := true

LOCAL_CFLAGS += -Wno-multichar -lrt

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE := media_extractor_test

include $(BUILD_EXECUTABLE)
