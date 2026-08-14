#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ---------- Mode ---------- */

enum StreamMode {
    MODE_BOTH,
    MODE_VIDEO,
    MODE_AUDIO
};

enum VideoCodec {
    VIDEO_CODEC_H264,
    VIDEO_CODEC_H265
};

static StreamMode g_mode = MODE_BOTH;
static VideoCodec g_video_codec = VIDEO_CODEC_H264;
static const char *g_host = "192.168.0.5";
static const char *g_mount_path = "/stream";
static const char *g_video_device = "/dev/video0";
static const char *g_audio_device = "plughw:CARD=rockchipes8388,DEV=0";
static guint g_port = 8554;
static gint g_video_width = 1280;
static gint g_video_height = 800;
static gint g_video_fps = 30;
static gboolean g_low_latency = TRUE;
static guint g_video_pt = 96;
static guint g_audio_pt = 8;
static gboolean g_quiet_rtspclient_logs = FALSE;

static const char *video_codec_name(void)
{
    return g_video_codec == VIDEO_CODEC_H265 ? "h265" : "h264";
}

static gboolean parse_int_arg(const char *value, gint min_v, gint max_v, gint *out)
{
    char *endptr = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(value, &endptr, 10);
    if (errno != 0 || endptr == value || *endptr != '\0') {
        return FALSE;
    }
    if (parsed < min_v || parsed > max_v) {
        return FALSE;
    }

    *out = (gint)parsed;
    return TRUE;
}

static gboolean parse_uint_arg(const char *value, guint min_v, guint max_v, guint *out)
{
    char *endptr = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(value, &endptr, 10);
    if (errno != 0 || endptr == value || *endptr != '\0') {
        return FALSE;
    }
    if (parsed < min_v || parsed > max_v) {
        return FALSE;
    }

    *out = (guint)parsed;
    return TRUE;
}

static void on_client_closed(GstRTSPClient *client, gpointer user_data)
{
    (void)user_data;
    GstRTSPConnection *conn = gst_rtsp_client_get_connection(client);
    const gchar *ip = conn ? gst_rtsp_connection_get_ip(conn) : NULL;

    g_print("RTSP client %p disconnected (ip=%s)\n", client, ip ? ip : "unknown");
}

static void on_client_connected(GstRTSPServer *server, GstRTSPClient *client, gpointer user_data)
{
    (void)server;
    (void)user_data;

    GstRTSPConnection *conn = gst_rtsp_client_get_connection(client);
    const gchar *ip = conn ? gst_rtsp_connection_get_ip(conn) : NULL;

    g_print("RTSP client %p connected (ip=%s)\n", client, ip ? ip : "unknown");
    g_signal_connect(client, "closed", G_CALLBACK(on_client_closed), NULL);
}

/* ---------- Custom factory ---------- */

typedef struct _MyFactory {
    GstRTSPMediaFactory parent;
} MyFactory;

typedef struct _MyFactoryClass {
    GstRTSPMediaFactoryClass parent_class;
} MyFactoryClass;

G_DEFINE_TYPE(MyFactory, my_factory, GST_TYPE_RTSP_MEDIA_FACTORY)

static GstElement *my_factory_create_element(GstRTSPMediaFactory *factory, const GstRTSPUrl *url)
{
    (void)factory;
    (void)url;

    GstElement *bin = gst_bin_new("media_bin");
    if (!bin) {
        g_printerr("Failed to create bin\n");
        return NULL;
    }

    /* ---------- VIDEO ---------- */
    if (g_mode == MODE_VIDEO || g_mode == MODE_BOTH) {
        const char *encoder_factory = g_video_codec == VIDEO_CODEC_H265 ? "mpph265enc" : "mpph264enc";
        const char *parser_factory = g_video_codec == VIDEO_CODEC_H265 ? "h265parse" : "h264parse";
        const char *payloader_factory = g_video_codec == VIDEO_CODEC_H265 ? "rtph265pay" : "rtph264pay";
        GstElement *v4l2src   = gst_element_factory_make("v4l2src", "v4l2src0");
        GstElement *v_capsf   = gst_element_factory_make("capsfilter", "v_caps");
        GstElement *v_queue   = gst_element_factory_make("queue", "v_queue");
        GstElement *encoder   = gst_element_factory_make(encoder_factory, "video_encoder0");
        GstElement *parser    = gst_element_factory_make(parser_factory, "video_parser0");
        GstElement *pay_video = gst_element_factory_make(payloader_factory, "pay0");

        if (!v4l2src || !v_capsf || !v_queue || !encoder || !parser || !pay_video) {
            g_printerr("Failed to create %s video elements (%s, %s, %s)\n",
                       video_codec_name(), encoder_factory, parser_factory, payloader_factory);
            gst_object_unref(bin);
            return NULL;
        }

        GstCaps *v_caps = gst_caps_new_simple("video/x-raw",
                                              "width", G_TYPE_INT, g_video_width,
                                              "height", G_TYPE_INT, g_video_height,
                                              "framerate", GST_TYPE_FRACTION, g_video_fps, 1,
                                              NULL);

        g_object_set(v4l2src,
                     "device", g_video_device,
                     "io-mode", 5,
                     "do-timestamp", TRUE,
                     NULL);
        g_object_set(v_capsf, "caps", v_caps, NULL);
        g_object_set(pay_video, "pt", g_video_pt, "config-interval", 1, NULL);

        if (g_low_latency) {
            g_object_set(v_queue,
                         "max-size-buffers", 4,
                         "max-size-bytes", 0,
                         "max-size-time", (guint64)0,
                         "leaky", 2,
                         NULL);
        }

        gst_caps_unref(v_caps);

        gst_bin_add_many(GST_BIN(bin),
                         v4l2src, v_capsf, v_queue, encoder, parser, pay_video,
                         NULL);

        if (!gst_element_link_many(v4l2src, v_capsf, v_queue, encoder, parser, pay_video, NULL)) {
            g_printerr("Failed to link %s video chain\n", video_codec_name());
            gst_object_unref(bin);
            return NULL;
        }
    }

    /* ---------- AUDIO ---------- */
    if (g_mode == MODE_AUDIO || g_mode == MODE_BOTH) {
        GstElement *alsasrc       = gst_element_factory_make("alsasrc", "alsasrc0");
        GstElement *a_queue       = gst_element_factory_make("queue", "a_queue");
        GstElement *audioconvert  = gst_element_factory_make("audioconvert", "audioconvert0");
        GstElement *audioresample = gst_element_factory_make("audioresample", "audioresample0");
        GstElement *a_capsf       = gst_element_factory_make("capsfilter", "a_caps");
        GstElement *alawenc       = gst_element_factory_make("alawenc", "alawenc0");

        const char *audio_pay_name = (g_mode == MODE_AUDIO) ? "pay0" : "pay1";
        GstElement *pay_audio = gst_element_factory_make("rtppcmapay", audio_pay_name);

        if (!alsasrc || !a_queue || !audioconvert || !audioresample ||
            !a_capsf || !alawenc || !pay_audio) {
            g_printerr("Failed to create audio elements\n");
            gst_object_unref(bin);
            return NULL;
        }

        GstCaps *a_caps = gst_caps_new_simple("audio/x-raw",
                                            "format", G_TYPE_STRING, "S16LE",
                                            "rate", G_TYPE_INT, 8000,
                                            "channels", G_TYPE_INT, 1,
                                            NULL);

        g_object_set(alsasrc,
                     "device", g_audio_device,
                     "do-timestamp", TRUE,
                     NULL);

        g_object_set(a_capsf, "caps", a_caps, NULL);
        g_object_set(pay_audio, "pt", g_audio_pt, NULL);

        if (g_low_latency) {
            g_object_set(a_queue,
                         "max-size-buffers", 16,
                         "max-size-bytes", 0,
                         "max-size-time", (guint64)0,
                         "leaky", 2,
                         NULL);
        }

        gst_caps_unref(a_caps);

        gst_bin_add_many(GST_BIN(bin),
                        alsasrc, a_queue, audioconvert, audioresample,
                        a_capsf, alawenc, pay_audio,
                        NULL);

        if (!gst_element_link_many(alsasrc, a_queue, audioconvert, audioresample,
                                a_capsf, alawenc, pay_audio, NULL)) {
            g_printerr("Failed to link audio chain\n");
            gst_object_unref(bin);
            return NULL;
        }
    }

    return bin;
}

static void my_factory_class_init(MyFactoryClass *klass)
{
    GstRTSPMediaFactoryClass *factory_class = GST_RTSP_MEDIA_FACTORY_CLASS(klass);
    factory_class->create_element = my_factory_create_element;
}

static void my_factory_init(MyFactory *self)
{
    (void)self;
}

/* ---------- Helpers ---------- */

static void print_usage(const char *progname)
{
    g_print("Usage:\n");
    g_print("  %s                    # video + audio\n", progname);
    g_print("  %s --both             # video + audio\n", progname);
    g_print("  %s --video            # video only\n", progname);
    g_print("  %s --audio            # audio only\n", progname);
    g_print("  %s --port 8554 --mount /stream1 --host 192.168.0.5\n", progname);
    g_print("  %s --width 1280 --height 800 --fps 30\n", progname);
    g_print("  %s --codec h264|h265  # H.264 by default\n", progname);
    g_print("  %s --video-device /dev/video0 --audio-device plughw:CARD=...,DEV=0\n", progname);
    g_print("  %s --low-latency|--no-low-latency\n", progname);
    g_print("  %s --quiet-rtspclient-logs\n", progname);
}

static void parse_args(int argc, char *argv[])
{
    gint tmp_i = 0;
    guint tmp_u = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--video") == 0) {
            g_mode = MODE_VIDEO;
        } else if (strcmp(argv[i], "--audio") == 0) {
            g_mode = MODE_AUDIO;
        } else if (strcmp(argv[i], "--both") == 0) {
            g_mode = MODE_BOTH;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            if (!parse_uint_arg(argv[++i], 1, 65535, &tmp_u)) {
                g_printerr("Invalid --port value\n");
                exit(1);
            }
            g_port = tmp_u;
        } else if (strcmp(argv[i], "--mount") == 0 && i + 1 < argc) {
            g_mount_path = argv[++i];
            if (g_mount_path[0] != '/') {
                g_printerr("--mount must start with '/'\n");
                exit(1);
            }
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            g_host = argv[++i];
        } else if (strcmp(argv[i], "--video-device") == 0 && i + 1 < argc) {
            g_video_device = argv[++i];
        } else if (strcmp(argv[i], "--audio-device") == 0 && i + 1 < argc) {
            g_audio_device = argv[++i];
        } else if (strcmp(argv[i], "--codec") == 0) {
            if (i + 1 >= argc) {
                g_printerr("Missing --codec value (expected h264 or h265)\n");
                exit(1);
            }
            const char *codec = argv[++i];
            if (strcmp(codec, "h264") == 0) {
                g_video_codec = VIDEO_CODEC_H264;
            } else if (strcmp(codec, "h265") == 0) {
                g_video_codec = VIDEO_CODEC_H265;
            } else {
                g_printerr("Invalid --codec value: %s (expected h264 or h265)\n", codec);
                exit(1);
            }
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], 16, 8192, &tmp_i)) {
                g_printerr("Invalid --width value\n");
                exit(1);
            }
            g_video_width = tmp_i;
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], 16, 8192, &tmp_i)) {
                g_printerr("Invalid --height value\n");
                exit(1);
            }
            g_video_height = tmp_i;
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], 1, 240, &tmp_i)) {
                g_printerr("Invalid --fps value\n");
                exit(1);
            }
            g_video_fps = tmp_i;
        } else if (strcmp(argv[i], "--video-pt") == 0 && i + 1 < argc) {
            if (!parse_uint_arg(argv[++i], 0, 127, &tmp_u)) {
                g_printerr("Invalid --video-pt value\n");
                exit(1);
            }
            g_video_pt = tmp_u;
        } else if (strcmp(argv[i], "--audio-pt") == 0 && i + 1 < argc) {
            if (!parse_uint_arg(argv[++i], 0, 127, &tmp_u)) {
                g_printerr("Invalid --audio-pt value\n");
                exit(1);
            }
            g_audio_pt = tmp_u;
        } else if (strcmp(argv[i], "--low-latency") == 0) {
            g_low_latency = TRUE;
        } else if (strcmp(argv[i], "--no-low-latency") == 0) {
            g_low_latency = FALSE;
        } else if (strcmp(argv[i], "--quiet-rtspclient-logs") == 0) {
            g_quiet_rtspclient_logs = TRUE;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            g_printerr("Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }
}

/* ---------- Main ---------- */

int main(int argc, char *argv[])
{
    GMainLoop *loop;
    GstRTSPServer *server;
    GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *factory;
    guint id;

    parse_args(argc, argv);
    gst_init(&argc, &argv);
    if (g_quiet_rtspclient_logs) {
        gst_debug_set_threshold_for_name("rtspclient", GST_LEVEL_NONE);
    }

    loop = g_main_loop_new(NULL, FALSE);

    server = gst_rtsp_server_new();
    g_signal_connect(server, "client-connected", G_CALLBACK(on_client_connected), NULL);
    {
        char port_str[16];
        g_snprintf(port_str, sizeof(port_str), "%u", g_port);
        gst_rtsp_server_set_service(server, port_str);
    }

    mounts = gst_rtsp_server_get_mount_points(server);

    factory = GST_RTSP_MEDIA_FACTORY(g_object_new(my_factory_get_type(), NULL));
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    gst_rtsp_media_factory_set_stop_on_disconnect(factory, TRUE);
    gst_rtsp_media_factory_set_protocols(
        factory,
        (GstRTSPLowerTrans)(GST_RTSP_LOWER_TRANS_UDP |
                            GST_RTSP_LOWER_TRANS_UDP_MCAST |
                            GST_RTSP_LOWER_TRANS_TCP));
    // if (g_low_latency) {
    //     gst_rtsp_media_factory_set_latency(factory, 0);
    // }

    gst_rtsp_mount_points_add_factory(mounts, g_mount_path, factory);
    g_object_unref(mounts);

    id = gst_rtsp_server_attach(server, NULL);
    if (id == 0) {
        g_printerr("Failed to attach RTSP server\n");
        g_object_unref(server);
        g_main_loop_unref(loop);
        return 1;
    }

    switch (g_mode) {
        case MODE_VIDEO:
            g_print("RTSP %s video only: rtsp://%s:%u%s\n",
                    video_codec_name(), g_host, g_port, g_mount_path);
            break;
        case MODE_AUDIO:
            g_print("RTSP audio only: rtsp://%s:%u%s\n", g_host, g_port, g_mount_path);
            break;
        case MODE_BOTH:
        default:
            g_print("RTSP %s video+audio: rtsp://%s:%u%s\n",
                    video_codec_name(), g_host, g_port, g_mount_path);
            break;
    }

    g_main_loop_run(loop);

    g_object_unref(server);
    g_main_loop_unref(loop);
    return 0;
}
