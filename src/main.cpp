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
static const char *g_host = "0.0.0.0";
static const char *g_mount_path = "/stream";
static const char *g_secondary_mount_path = "/stream-low";
static const char *g_video_device = "/dev/video0";
static const char *g_audio_device = "plughw:CARD=rockchipes8388,DEV=0";
static guint g_port = 8554;
static gint g_video_width = 1280;
static gint g_video_height = 800;
static gint g_video_fps = 30;
static gint g_secondary_width = 640;
static gint g_secondary_height = 360;
static gboolean g_low_latency = TRUE;
static guint g_video_pt = 96;
static guint g_audio_pt = 8;
static gboolean g_quiet_rtspclient_logs = FALSE;
static gboolean g_tcp_only = FALSE;
static gboolean g_use_mpp = FALSE;
static gchar *g_mkv_path = NULL;
static gboolean g_mkv_has_video = FALSE;
static gboolean g_mkv_has_audio = FALSE;

static const char *video_codec_name(void)
{
    return g_video_codec == VIDEO_CODEC_H265 ? "h265" : "h264";
}

static const char *video_encoder_factory_name(void)
{
    if (g_use_mpp) {
        return g_video_codec == VIDEO_CODEC_H265 ? "mpph265enc" : "mpph264enc";
    }
    return g_video_codec == VIDEO_CODEC_H265 ? "x265enc" : "x264enc";
}

static const char *video_encoder_raw_format(void)
{
    return g_use_mpp ? "NV12" : "I420";
}

static void configure_video_encoder(GstElement *encoder)
{
    if (!g_use_mpp && g_low_latency) {
        g_object_set(encoder,
                     "speed-preset", 1,
                     "tune", 4,
                     "key-int-max", g_video_fps,
                     NULL);
    }
}

typedef struct {
    gboolean has_video;
    gboolean has_audio;
    gboolean video_supported;
    gboolean done;
    VideoCodec video_codec;
} MkvProbeResult;

static void on_mkv_probe_pad_added(GstElement *demux, GstPad *src_pad, gpointer user_data)
{
    (void)demux;
    MkvProbeResult *result = (MkvProbeResult *)user_data;
    GstCaps *caps = gst_pad_get_current_caps(src_pad);
    if (!caps) {
        caps = gst_pad_query_caps(src_pad, NULL);
    }

    if (caps && !gst_caps_is_empty(caps)) {
        const gchar *media_type = gst_structure_get_name(gst_caps_get_structure(caps, 0));
        if (g_str_has_prefix(media_type, "video/") && !result->has_video) {
            result->has_video = TRUE;
            if (strcmp(media_type, "video/x-h264") == 0) {
                result->video_supported = TRUE;
                result->video_codec = VIDEO_CODEC_H264;
            } else if (strcmp(media_type, "video/x-h265") == 0) {
                result->video_supported = TRUE;
                result->video_codec = VIDEO_CODEC_H265;
            }
        } else if (g_str_has_prefix(media_type, "audio/")) {
            result->has_audio = TRUE;
        }
    }

    GstElement *pipeline = GST_ELEMENT(gst_element_get_parent(demux));
    GstElement *sink = gst_element_factory_make("fakesink", NULL);
    if (pipeline && sink) {
        g_object_set(sink, "async", FALSE, "sync", FALSE, NULL);
        gst_bin_add(GST_BIN(pipeline), sink);
        GstPad *sink_pad = gst_element_get_static_pad(sink, "sink");
        gst_pad_link(src_pad, sink_pad);
        gst_object_unref(sink_pad);
        gst_element_sync_state_with_parent(sink);
        gst_element_post_message(
            pipeline,
            gst_message_new_application(
                GST_OBJECT(demux), gst_structure_new_empty("mkv-probe-pad-added")));
    } else if (sink) {
        gst_object_unref(sink);
    }
    if (pipeline) {
        gst_object_unref(pipeline);
    }
    if (caps) {
        gst_caps_unref(caps);
    }
}

static void on_mkv_probe_no_more_pads(GstElement *demux, gpointer user_data)
{
    MkvProbeResult *result = (MkvProbeResult *)user_data;
    result->done = TRUE;
    GstElement *pipeline = GST_ELEMENT(gst_element_get_parent(demux));
    if (pipeline) {
        gst_element_post_message(
            pipeline,
            gst_message_new_application(
                GST_OBJECT(demux), gst_structure_new_empty("mkv-probe-done")));
        gst_object_unref(pipeline);
    }
}

static gboolean probe_mkv_file(void)
{
    GstElement *pipeline = gst_pipeline_new("mkv_probe_pipeline");
    GstElement *source = gst_element_factory_make("filesrc", "mkv_probe_source");
    GstElement *demux = gst_element_factory_make("matroskademux", "mkv_probe_demux");
    MkvProbeResult result = {};

    if (!pipeline || !source || !demux) {
        g_printerr("Failed to create elements for probing MKV file\n");
        if (pipeline) {
            gst_object_unref(pipeline);
        }
        if (source) {
            gst_object_unref(source);
        }
        if (demux) {
            gst_object_unref(demux);
        }
        return FALSE;
    }

    g_object_set(source, "location", g_mkv_path, NULL);
    gst_bin_add_many(GST_BIN(pipeline), source, demux, NULL);
    if (!gst_element_link(source, demux)) {
        g_printerr("Failed to link elements for probing MKV file\n");
        gst_object_unref(pipeline);
        return FALSE;
    }
    g_signal_connect(demux, "pad-added", G_CALLBACK(on_mkv_probe_pad_added), &result);
    g_signal_connect(demux, "no-more-pads", G_CALLBACK(on_mkv_probe_no_more_pads), &result);

    GstBus *bus = gst_element_get_bus(pipeline);
    GstStateChangeReturn state_result = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    gboolean probe_failed = state_result == GST_STATE_CHANGE_FAILURE;
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    while (!probe_failed) {
        if (result.done) {
            break;
        }

        gint64 remaining_us = deadline - g_get_monotonic_time();
        if (remaining_us <= 0) {
            break;
        }
        GstMessage *message = gst_bus_timed_pop_filtered(
            bus,
            (GstClockTime)remaining_us * GST_USECOND,
            (GstMessageType)(GST_MESSAGE_APPLICATION | GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (!message) {
            break;
        }
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError *error = NULL;
            gchar *debug_info = NULL;
            gst_message_parse_error(message, &error, &debug_info);
            g_printerr("Failed to inspect MKV file: %s\n",
                       error ? error->message : "unknown GStreamer error");
            g_clear_error(&error);
            g_free(debug_info);
            probe_failed = TRUE;
        }
        gst_message_unref(message);
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);

    if (probe_failed) {
        return FALSE;
    }
    if (!result.has_video && !result.has_audio) {
        g_printerr("Selected MKV file has no supported audio or video streams\n");
        return FALSE;
    }
    if (result.has_video && !result.video_supported) {
        g_printerr("MKV passthrough supports H.264 and H.265 video only\n");
        return FALSE;
    }

    if (result.has_video && result.video_supported) {
        g_video_codec = result.video_codec;
    }
    g_mkv_has_video = result.has_video;
    g_mkv_has_audio = result.has_audio;
    if (g_mkv_has_video && g_mkv_has_audio) {
        g_mode = MODE_BOTH;
    } else if (g_mkv_has_video) {
        g_mode = MODE_VIDEO;
    } else {
        g_mode = MODE_AUDIO;
    }
    return TRUE;
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

    g_print("RTSP client %p disconnected (ip=%s)\n", (void *)client, ip ? ip : "unknown");
}

static void on_client_connected(GstRTSPServer *server, GstRTSPClient *client, gpointer user_data)
{
    (void)server;
    (void)user_data;

    GstRTSPConnection *conn = gst_rtsp_client_get_connection(client);
    const gchar *ip = conn ? gst_rtsp_connection_get_ip(conn) : NULL;

    g_print("RTSP client %p connected (ip=%s)\n", (void *)client, ip ? ip : "unknown");
    g_signal_connect(client, "closed", G_CALLBACK(on_client_closed), NULL);
}

/* ---------- Custom factory ---------- */

typedef struct _MyFactory {
    GstRTSPMediaFactory parent;
    gboolean reduced_resolution;
} MyFactory;

typedef struct _MyFactoryClass {
    GstRTSPMediaFactoryClass parent_class;
} MyFactoryClass;

G_DEFINE_TYPE(MyFactory, my_factory, GST_TYPE_RTSP_MEDIA_FACTORY)

typedef struct {
    GstElement *video_queue;
    GstElement *audio_queue;
} MkvPadTargets;

static void on_mkv_pad_added(GstElement *decodebin, GstPad *src_pad, gpointer user_data)
{
    (void)decodebin;
    MkvPadTargets *targets = (MkvPadTargets *)user_data;
    GstCaps *caps = gst_pad_get_current_caps(src_pad);
    if (!caps) {
        caps = gst_pad_query_caps(src_pad, NULL);
    }
    if (!caps || gst_caps_is_empty(caps)) {
        if (caps) {
            gst_caps_unref(caps);
        }
        return;
    }

    const GstStructure *structure = gst_caps_get_structure(caps, 0);
    const gchar *media_type = gst_structure_get_name(structure);
    GstElement *target = NULL;

    if (g_str_has_prefix(media_type, "video/x-raw") ||
        strcmp(media_type, "video/x-h264") == 0 ||
        strcmp(media_type, "video/x-h265") == 0) {
        target = targets->video_queue;
    } else if (g_str_has_prefix(media_type, "audio/x-raw")) {
        target = targets->audio_queue;
    }

    if (target) {
        GstPad *sink_pad = gst_element_get_static_pad(target, "sink");
        if (!gst_pad_is_linked(sink_pad)) {
            GstPadLinkReturn result = gst_pad_link(src_pad, sink_pad);
            if (result != GST_PAD_LINK_OK) {
                g_printerr("Failed to link MKV %s stream: %s\n",
                           media_type, gst_pad_link_get_name(result));
            }
        }
        gst_object_unref(sink_pad);
    }

    gst_caps_unref(caps);
}

static GstElement *create_mkv_element(gboolean reduced_resolution)
{
    GstElement *bin = gst_bin_new("mkv_media_bin");
    GstElement *decodebin = gst_element_factory_make("uridecodebin", "mkv_decoder0");
    GError *uri_error = NULL;
    gchar *uri = gst_filename_to_uri(g_mkv_path, &uri_error);

    if (!bin || !decodebin || !uri) {
        g_printerr("Failed to create MKV source for %s%s%s\n",
                   g_mkv_path,
                   uri_error ? ": " : "",
                   uri_error ? uri_error->message : "");
        if (bin) {
            gst_object_unref(bin);
        }
        if (decodebin) {
            gst_object_unref(decodebin);
        }
        g_free(uri);
        g_clear_error(&uri_error);
        return NULL;
    }

    g_object_set(decodebin, "uri", uri, NULL);
    GstCaps *decode_caps = gst_caps_from_string(
        reduced_resolution
            ? "video/x-raw; audio/x-raw"
            : "video/x-h264; video/x-h265; audio/x-raw");
    g_object_set(decodebin, "caps", decode_caps, NULL);
    gst_caps_unref(decode_caps);
    g_free(uri);
    g_clear_error(&uri_error);
    gst_bin_add(GST_BIN(bin), decodebin);

    MkvPadTargets *targets = g_new0(MkvPadTargets, 1);
    g_object_set_data_full(G_OBJECT(decodebin), "mkv-pad-targets", targets, g_free);

    if (g_mkv_has_video) {
        const char *parser_factory = g_video_codec == VIDEO_CODEC_H265 ? "h265parse" : "h264parse";
        const char *payloader_factory = g_video_codec == VIDEO_CODEC_H265 ? "rtph265pay" : "rtph264pay";
        GstElement *queue = gst_element_factory_make("queue", "mkv_video_queue");
        GstElement *parser = gst_element_factory_make(parser_factory, "mkv_video_parser0");
        GstElement *pay = gst_element_factory_make(payloader_factory, "pay0");

        if (!queue || !parser || !pay) {
            g_printerr("Failed to create MKV %s passthrough elements (%s, %s)\n",
                       video_codec_name(), parser_factory, payloader_factory);
            gst_object_unref(bin);
            return NULL;
        }

        g_object_set(pay, "pt", g_video_pt, "config-interval", 1, NULL);

        if (reduced_resolution) {
            const char *encoder_factory = video_encoder_factory_name();
            GstElement *convert = gst_element_factory_make("videoconvert", "mkv_low_videoconvert0");
            GstElement *scale = gst_element_factory_make("videoscale", "mkv_low_videoscale0");
            GstElement *capsfilter = gst_element_factory_make("capsfilter", "mkv_low_video_caps");
            GstElement *encoder = gst_element_factory_make(encoder_factory, "mkv_low_video_encoder0");

            if (!convert || !scale || !capsfilter || !encoder) {
                g_printerr("Failed to create reduced MKV video elements (%s)\n", encoder_factory);
                gst_object_unref(bin);
                return NULL;
            }

            GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                                "format", G_TYPE_STRING, video_encoder_raw_format(),
                                                "width", G_TYPE_INT, g_secondary_width,
                                                "height", G_TYPE_INT, g_secondary_height,
                                                NULL);
            g_object_set(scale, "add-borders", TRUE, NULL);
            g_object_set(capsfilter, "caps", caps, NULL);
            configure_video_encoder(encoder);
            gst_caps_unref(caps);

            gst_bin_add_many(GST_BIN(bin),
                             queue, convert, scale, capsfilter, encoder, parser, pay,
                             NULL);
            if (!gst_element_link_many(
                    queue, convert, scale, capsfilter, encoder, parser, pay, NULL)) {
                g_printerr("Failed to link reduced MKV %s video chain\n", video_codec_name());
                gst_object_unref(bin);
                return NULL;
            }
        } else {
            gst_bin_add_many(GST_BIN(bin), queue, parser, pay, NULL);
            if (!gst_element_link_many(queue, parser, pay, NULL)) {
                g_printerr("Failed to link MKV %s passthrough chain\n", video_codec_name());
                gst_object_unref(bin);
                return NULL;
            }
        }
        targets->video_queue = queue;
    }

    if (g_mkv_has_audio) {
        const char *audio_pay_name = g_mkv_has_video ? "pay1" : "pay0";
        GstElement *queue = gst_element_factory_make("queue", "mkv_audio_queue");
        GstElement *convert = gst_element_factory_make("audioconvert", "mkv_audioconvert0");
        GstElement *resample = gst_element_factory_make("audioresample", "mkv_audioresample0");
        GstElement *capsfilter = gst_element_factory_make("capsfilter", "mkv_audio_caps");
        GstElement *alawenc = gst_element_factory_make("alawenc", "mkv_alawenc0");
        GstElement *pay = gst_element_factory_make("rtppcmapay", audio_pay_name);

        if (!queue || !convert || !resample || !capsfilter || !alawenc || !pay) {
            g_printerr("Failed to create MKV audio elements\n");
            gst_object_unref(bin);
            return NULL;
        }

        GstCaps *caps = gst_caps_new_simple("audio/x-raw",
                                            "format", G_TYPE_STRING, "S16LE",
                                            "rate", G_TYPE_INT, 8000,
                                            "channels", G_TYPE_INT, 1,
                                            NULL);
        g_object_set(capsfilter, "caps", caps, NULL);
        g_object_set(pay, "pt", g_audio_pt, NULL);
        gst_caps_unref(caps);

        gst_bin_add_many(GST_BIN(bin), queue, convert, resample, capsfilter, alawenc, pay, NULL);
        if (!gst_element_link_many(queue, convert, resample, capsfilter, alawenc, pay, NULL)) {
            g_printerr("Failed to link MKV audio chain\n");
            gst_object_unref(bin);
            return NULL;
        }
        targets->audio_queue = queue;
    }

    g_signal_connect(decodebin, "pad-added", G_CALLBACK(on_mkv_pad_added), targets);
    return bin;
}

static GstElement *my_factory_create_element(GstRTSPMediaFactory *factory, const GstRTSPUrl *url)
{
    (void)url;
    gboolean reduced_resolution = ((MyFactory *)factory)->reduced_resolution;

    if (g_mkv_path) {
        return create_mkv_element(reduced_resolution);
    }

    GstElement *bin = gst_bin_new("media_bin");
    if (!bin) {
        g_printerr("Failed to create bin\n");
        return NULL;
    }

    /* ---------- VIDEO ---------- */
    if (g_mode == MODE_VIDEO || g_mode == MODE_BOTH) {
        const char *encoder_factory = video_encoder_factory_name();
        const char *parser_factory = g_video_codec == VIDEO_CODEC_H265 ? "h265parse" : "h264parse";
        const char *payloader_factory = g_video_codec == VIDEO_CODEC_H265 ? "rtph265pay" : "rtph264pay";
        gboolean needs_convert = reduced_resolution || !g_use_mpp;
        GstElement *v4l2src   = gst_element_factory_make("v4l2src", "v4l2src0");
        GstElement *v_capsf   = gst_element_factory_make("capsfilter", "v_caps");
        GstElement *v_queue   = gst_element_factory_make("queue", "v_queue");
        GstElement *v_convert = needs_convert
                                    ? gst_element_factory_make("videoconvert", "video_convert0")
                                    : NULL;
        GstElement *v_scale   = reduced_resolution
                                    ? gst_element_factory_make("videoscale", "low_videoscale0")
                                    : NULL;
        GstElement *v_low_capsf = reduced_resolution
                                      ? gst_element_factory_make("capsfilter", "low_video_caps")
                                      : NULL;
        GstElement *encoder   = gst_element_factory_make(encoder_factory, "video_encoder0");
        GstElement *parser    = gst_element_factory_make(parser_factory, "video_parser0");
        GstElement *pay_video = gst_element_factory_make(payloader_factory, "pay0");

        if (!v4l2src || !v_capsf || !v_queue || !encoder || !parser || !pay_video ||
            (needs_convert && !v_convert) ||
            (reduced_resolution && (!v_scale || !v_low_capsf))) {
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
        configure_video_encoder(encoder);

        if (reduced_resolution) {
            GstCaps *low_caps = gst_caps_new_simple("video/x-raw",
                                                    "format", G_TYPE_STRING, video_encoder_raw_format(),
                                                    "width", G_TYPE_INT, g_secondary_width,
                                                    "height", G_TYPE_INT, g_secondary_height,
                                                    NULL);
            g_object_set(v_scale, "add-borders", TRUE, NULL);
            g_object_set(v_low_capsf, "caps", low_caps, NULL);
            gst_caps_unref(low_caps);
        }

        if (g_low_latency) {
            g_object_set(v_queue,
                         "max-size-buffers", 4,
                         "max-size-bytes", 0,
                         "max-size-time", (guint64)0,
                         "leaky", 2,
                         NULL);
        }

        gst_caps_unref(v_caps);

        if (reduced_resolution) {
            gst_bin_add_many(GST_BIN(bin),
                             v4l2src, v_capsf, v_queue, v_convert, v_scale, v_low_capsf,
                             encoder, parser, pay_video, NULL);
            if (!gst_element_link_many(v4l2src, v_capsf, v_queue,
                                       v_convert, v_scale, v_low_capsf,
                                       encoder, parser, pay_video, NULL)) {
                g_printerr("Failed to link reduced %s camera video chain\n", video_codec_name());
                gst_object_unref(bin);
                return NULL;
            }
        } else {
            if (needs_convert) {
                gst_bin_add_many(GST_BIN(bin),
                                 v4l2src, v_capsf, v_queue, v_convert,
                                 encoder, parser, pay_video, NULL);
            } else {
                gst_bin_add_many(GST_BIN(bin),
                                 v4l2src, v_capsf, v_queue,
                                 encoder, parser, pay_video, NULL);
            }
            gboolean linked = needs_convert
                                  ? gst_element_link_many(v4l2src, v_capsf, v_queue, v_convert,
                                                          encoder, parser, pay_video, NULL)
                                  : gst_element_link_many(v4l2src, v_capsf, v_queue,
                                                          encoder, parser, pay_video, NULL);
            if (!linked) {
                g_printerr("Failed to link %s video chain\n", video_codec_name());
                gst_object_unref(bin);
                return NULL;
            }
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
    self->reduced_resolution = FALSE;
}

/* ---------- Helpers ---------- */

static void print_usage(const char *progname)
{
    g_print("Usage:\n");
    g_print("  %s                    # video + audio\n", progname);
    g_print("  %s --both             # video + audio\n", progname);
    g_print("  %s --video            # video only\n", progname);
    g_print("  %s --audio            # audio only\n", progname);
    g_print("  %s --port 8554 --mount /stream1 --host 0.0.0.0\n", progname);
    g_print("  %s --secondary-mount /stream-low --secondary-width 640 --secondary-height 360\n",
            progname);
    g_print("  %s --width 1280 --height 800 --fps 30\n", progname);
    g_print("  %s --codec h264|h265  # camera mode only; H.264 by default\n", progname);
    g_print("  %s --mpp              # force mpph264enc/mpph265enc instead of x264enc/x265enc\n",
            progname);
    g_print("  %s --mkv FILE         # auto-detect tracks from FILE in ./mkv_files\n", progname);
    g_print("  %s --mkv 0391_53_50.mkv\n", progname);
    g_print("  %s --video-device /dev/video0 --audio-device plughw:CARD=...,DEV=0\n", progname);
    g_print("  %s --low-latency|--no-low-latency\n", progname);
    g_print("  %s --tcp-only         # allow RTP over RTSP/TCP only\n", progname);
    g_print("  %s --quiet-rtspclient-logs\n", progname);
}

static gchar *resolve_mkv_path(const char *value)
{
    gchar *cwd = g_get_current_dir();
    gchar *mkv_dir = g_build_filename(cwd, "mkv_files", NULL);
    gchar *candidate;

    if (g_path_is_absolute(value)) {
        candidate = g_strdup(value);
    } else if (g_str_has_prefix(value, "mkv_files/") ||
               g_str_has_prefix(value, "mkv_files\\")) {
        candidate = g_build_filename(cwd, value, NULL);
    } else {
        candidate = g_build_filename(mkv_dir, value, NULL);
    }

    char *resolved_dir = realpath(mkv_dir, NULL);
    char *resolved_file = realpath(candidate, NULL);
    g_free(candidate);
    g_free(mkv_dir);
    g_free(cwd);

    if (!resolved_dir || !resolved_file) {
        g_printerr("MKV file not found: %s\n", value);
        free(resolved_dir);
        free(resolved_file);
        return NULL;
    }

    size_t dir_len = strlen(resolved_dir);
    gboolean inside_mkv_dir = strncmp(resolved_file, resolved_dir, dir_len) == 0 &&
                              resolved_file[dir_len] == G_DIR_SEPARATOR;
    const char *extension = strrchr(resolved_file, '.');
    gboolean is_mkv = extension && g_ascii_strcasecmp(extension, ".mkv") == 0;

    if (!inside_mkv_dir || !is_mkv || !g_file_test(resolved_file, G_FILE_TEST_IS_REGULAR)) {
        g_printerr("--mkv must point to an .mkv file inside ./mkv_files: %s\n", value);
        free(resolved_dir);
        free(resolved_file);
        return NULL;
    }

    gchar *result = g_strdup(resolved_file);
    free(resolved_dir);
    free(resolved_file);
    return result;
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
        } else if (strcmp(argv[i], "--secondary-mount") == 0 && i + 1 < argc) {
            g_secondary_mount_path = argv[++i];
            if (g_secondary_mount_path[0] != '/') {
                g_printerr("--secondary-mount must start with '/'\n");
                exit(1);
            }
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            g_host = argv[++i];
        } else if (strcmp(argv[i], "--video-device") == 0 && i + 1 < argc) {
            g_video_device = argv[++i];
        } else if (strcmp(argv[i], "--audio-device") == 0 && i + 1 < argc) {
            g_audio_device = argv[++i];
        } else if (strcmp(argv[i], "--mkv") == 0) {
            if (i + 1 >= argc) {
                g_printerr("Missing --mkv value\n");
                exit(1);
            }
            g_free(g_mkv_path);
            g_mkv_path = resolve_mkv_path(argv[++i]);
            if (!g_mkv_path) {
                exit(1);
            }
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
        } else if (strcmp(argv[i], "--secondary-width") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], 16, 8192, &tmp_i)) {
                g_printerr("Invalid --secondary-width value\n");
                exit(1);
            }
            g_secondary_width = tmp_i;
        } else if (strcmp(argv[i], "--secondary-height") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], 16, 8192, &tmp_i)) {
                g_printerr("Invalid --secondary-height value\n");
                exit(1);
            }
            g_secondary_height = tmp_i;
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
        } else if (strcmp(argv[i], "--tcp-only") == 0) {
            g_tcp_only = TRUE;
        } else if (strcmp(argv[i], "--mpp") == 0) {
            g_use_mpp = TRUE;
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

    if (strcmp(g_mount_path, g_secondary_mount_path) == 0) {
        g_printerr("--mount and --secondary-mount must be different\n");
        exit(1);
    }
}

static GstRTSPMediaFactory *create_rtsp_factory(gboolean reduced_resolution)
{
    MyFactory *custom_factory = (MyFactory *)g_object_new(my_factory_get_type(), NULL);
    custom_factory->reduced_resolution = reduced_resolution;
    GstRTSPMediaFactory *factory = GST_RTSP_MEDIA_FACTORY(custom_factory);

    gst_rtsp_media_factory_set_shared(factory, TRUE);
    gst_rtsp_media_factory_set_stop_on_disconnect(factory, TRUE);
    gst_rtsp_media_factory_set_protocols(
        factory,
        g_tcp_only
            ? GST_RTSP_LOWER_TRANS_TCP
            : (GstRTSPLowerTrans)(GST_RTSP_LOWER_TRANS_UDP |
                                  GST_RTSP_LOWER_TRANS_UDP_MCAST |
                                  GST_RTSP_LOWER_TRANS_TCP));
    return factory;
}

/* ---------- Main ---------- */

int main(int argc, char *argv[])
{
    GMainLoop *loop;
    GstRTSPServer *server;
    GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *factory;
    GstRTSPMediaFactory *secondary_factory = NULL;
    guint id;

    parse_args(argc, argv);
    gst_init(&argc, &argv);
    if (g_mkv_path && !probe_mkv_file()) {
        g_free(g_mkv_path);
        return 1;
    }
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

    factory = create_rtsp_factory(FALSE);
    // if (g_low_latency) {
    //     gst_rtsp_media_factory_set_latency(factory, 0);
    // }

    gst_rtsp_mount_points_add_factory(mounts, g_mount_path, factory);
    if (g_mode != MODE_AUDIO) {
        secondary_factory = create_rtsp_factory(TRUE);
        gst_rtsp_mount_points_add_factory(
            mounts, g_secondary_mount_path, secondary_factory);
    }
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
            g_print("RTSP %s video only%s: rtsp://%s:%u%s\n",
                    video_codec_name(), g_mkv_path ? " (MKV passthrough)" : "",
                    g_host, g_port, g_mount_path);
            break;
        case MODE_AUDIO:
            g_print("RTSP audio only%s: rtsp://%s:%u%s\n",
                    g_mkv_path ? " (MKV source)" : "", g_host, g_port, g_mount_path);
            break;
        case MODE_BOTH:
        default:
            g_print("RTSP %s video+audio%s: rtsp://%s:%u%s\n",
                    video_codec_name(), g_mkv_path ? " (MKV video passthrough)" : "",
                    g_host, g_port, g_mount_path);
            break;
    }
    if (secondary_factory) {
        g_print("RTSP %s reduced video %dx%d%s: rtsp://%s:%u%s\n",
                video_codec_name(), g_secondary_width, g_secondary_height,
                g_mode == MODE_BOTH ? "+audio" : "",
                g_host, g_port, g_secondary_mount_path);
    }
    if (g_mode != MODE_AUDIO) {
        g_print("Video encoder%s: %s\n",
                g_mkv_path ? " for reduced stream" : "",
                video_encoder_factory_name());
    }
    if (g_tcp_only) {
        g_print("RTP transport: TCP only\n");
    } else {
        g_print("RTP transport: UDP, UDP multicast, TCP\n");
    }

    g_main_loop_run(loop);

    g_object_unref(server);
    g_main_loop_unref(loop);
    g_free(g_mkv_path);
    return 0;
}
