LOCAL_PATH := $(call my-dir)

# This allows to build both with and without PIE.
TARGET_PIE := false
NDK_APP_PIE := false

# Readline support with ndk-build
# ifeq ($(WITH_READLINE),)
# WITH_READLINE=$(HAVE_LIBREADLINE)
# endif
# ifeq ($(WITH_READLINE),)
# $(warning )
# $(warning WARNING: WITH_READLINE was not specified, so a libreadline replacement will be used.)
# $(warning )
# $(shell sleep 2)
# else
# readline and ncurses may require PREBUILT_STATIC_LIBRARY and LOCAL_STATIC_LIBRARIES
# endif

include $(CLEAR_VARS)

# Static library, no need for jni.
LOCAL_MODULE := scanmem_static-pie

LOCAL_CFLAGS += -fPIE
LOCAL_C_FLAGS += -O2 -g -Wall -Werror -Wno-extra-portability foreign
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/..

LOCAL_SRC_FILES := \
    ../getline.c \
    ../readline.c \
    ../commands.c \
    ../handlers.c \
    ../list.c \
    ../maps.c \
    ../menu.c \
    ../ptrace.c \
    ../scanmem.c \
    ../scanroutines.c \
    ../show_message.c \
    ../target_memory_info_array.c \
    ../value.c

include $(BUILD_STATIC_LIBRARY)
include $(CLEAR_VARS)

# Shared library, with jni..
LOCAL_MODULE := scanmem_jni-pie

LOCAL_CFLAGS += -fPIE
LOCAL_C_FLAGS += -O2 -g -Wall -Werror -Wno-extra-portability foreign
LOCAL_STATIC_LIBRARIES := scanmem_static-pie
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/..

LOCAL_SRC_FILES := ../scanmem_jni.c

include $(BUILD_SHARED_LIBRARY)
include $(CLEAR_VARS)

# Executable, no jni.
LOCAL_MODULE := scanmem-pie

LOCAL_LDFLAGS += -fPIE -pie
LOCAL_CFLAGS += -fPIE
LOCAL_C_FLAGS += -O2 -g -Wall -Werror -Wno-extra-portability foreign
LOCAL_STATIC_LIBRARIES := scanmem_static-pie
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/..

LOCAL_SRC_FILES := ../main.c

include $(BUILD_EXECUTABLE)
include $(CLEAR_VARS)

# Static without PIE
LOCAL_MODULE := scanmem_static

LOCAL_C_FLAGS += -O2 -g -Wall -Werror -Wno-extra-portability foreign
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/..

LOCAL_SRC_FILES := \
    ../getline.c \
    ../readline.c \
    ../commands.c \
    ../handlers.c \
    ../list.c \
    ../maps.c \
    ../menu.c \
    ../ptrace.c \
    ../scanmem.c \
    ../scanroutines.c \
    ../show_message.c \
    ../target_memory_info_array.c \
    ../value.c

include $(BUILD_STATIC_LIBRARY)
include $(CLEAR_VARS)

# Shared without PIE, with jni.
LOCAL_MODULE := scanmem_jni

LOCAL_C_FLAGS += -O2 -g -Wall -Werror -Wno-extra-portability foreign
LOCAL_STATIC_LIBRARIES := scanmem_static
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/..

LOCAL_SRC_FILES := ../scanmem_jni.c

include $(BUILD_SHARED_LIBRARY)
include $(CLEAR_VARS)

# Executable without PIE.
LOCAL_MODULE := scanmem

LOCAL_C_FLAGS += -O2 -g -Wall -Werror -Wno-extra-portability foreign
LOCAL_STATIC_LIBRARIES := scanmem_static
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/..

LOCAL_SRC_FILES := ../main.c

include $(BUILD_EXECUTABLE)
