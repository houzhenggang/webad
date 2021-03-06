LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE:= webad

LOCAL_SRC_FILES := \
    main.c \
    libnetfilter_queue.c \
    mpool.c \
    msocket.c \
    thpool.c \
    task.c \
    mstring.c \
    util.c \
    tcp_stream.c \
    http_session.c \
    plug.c \
    plug_extern.c 
    
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../libnfnetlink/include/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../libmnl/include/
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../libnetfilter_queue/include/

LOCAL_MODULE_TAGS := optional

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libc libmnl libnfnetlink libnetfilter_queue


include $(BUILD_EXECUTABLE)
