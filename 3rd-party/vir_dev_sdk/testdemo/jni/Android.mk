# Copyright 2013 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH:= $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE := libsharedbufferclient
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../libs/libsharedbufferclient.sbc.so
include $(PREBUILT_SHARED_LIBRARY)


include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	$(LOCAL_PATH)/../testdemo.cpp \


LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../../inc/ \


LOCAL_LDLIBS := -llog

LOCAL_SHARED_LIBRARIES := \
	libsharedbufferclient \


# Enable PIE manually. Will get reset on $(CLEAR_VARS).
# LOCAL_CFLAGS += -fPIE
# LOCAL_LDFLAGS += -fPIE -pie

# LOCAL_PRELINK_MODULE := false

# LOCAL_VENDOR_MODULE := true
# LOCAL_MODULE_RELATIVE_PATH := hw

# LOCAL_CFLAGS += -Wno-multichar -lrt
#LOCAL_CFLAGS += -UNDEBUG

# LOCAL_CFLAGS += -m64

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= testdemo

include $(BUILD_EXECUTABLE)

