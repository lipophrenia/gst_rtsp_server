#pragma once

#include <gst/gst.h>

#include <memory>

template <typename T>
struct GstObjectDeleter {
    void operator()(T *object) const
    {
        if (object) {
            gst_object_unref(object);
        }
    }
};

template <typename T>
using GstObjectPtr = std::unique_ptr<T, GstObjectDeleter<T>>;

template <typename T>
struct GObjectDeleter {
    void operator()(T *object) const
    {
        if (object) {
            g_object_unref(object);
        }
    }
};

template <typename T>
using GObjectPtr = std::unique_ptr<T, GObjectDeleter<T>>;

struct GstCapsDeleter {
    void operator()(GstCaps *caps) const
    {
        if (caps) {
            gst_caps_unref(caps);
        }
    }
};

using GstCapsPtr = std::unique_ptr<GstCaps, GstCapsDeleter>;

struct GstMessageDeleter {
    void operator()(GstMessage *message) const
    {
        if (message) {
            gst_message_unref(message);
        }
    }
};

using GstMessagePtr = std::unique_ptr<GstMessage, GstMessageDeleter>;

struct GMainLoopDeleter {
    void operator()(GMainLoop *loop) const
    {
        if (loop) {
            g_main_loop_unref(loop);
        }
    }
};

using GMainLoopPtr = std::unique_ptr<GMainLoop, GMainLoopDeleter>;

struct GFreeDeleter {
    void operator()(void *memory) const
    {
        g_free(memory);
    }
};

using GCharPtr = std::unique_ptr<gchar, GFreeDeleter>;
