LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := FxsFuncs
LOCAL_SRC_FILES  := main.cpp
LOCAL_CPPFLAGS   := -std=c++17 -O2 -fno-rtti -fno-exceptions \
                    -march=armv7-a -mthumb -mfpu=neon
LOCAL_LDLIBS     := -llog -landroid
LOCAL_LDFLAGS    := -Wl,--gc-sections

include $(BUILD_SHARED_LIBRARY)
