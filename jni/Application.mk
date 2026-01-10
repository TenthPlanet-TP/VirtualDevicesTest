
# APP_ABI := armeabi-v7a arm64-v8a
APP_ABI := arm64-v8a

APP_BUILD_SCRIPT := Android.mk

# none / system / c++_static(default) / c++_shared
# if build so, open it, otherwise comment it out 
# APP_STL := c++_shared
APP_STL := c++_static

# APP_PLATFORM := android-24
# APP_PLATFORM := android-27
# APP_PLATFORM := android-28
APP_PLATFORM := android-29

# APP_CFLAGS := -frtti
# APP_CFLAGS += -Werror-return-type
# APP_OPTIM := debug

ifeq ($(NDK_DEBUG),1)
  APP_STRIP_MODE := none
endif

