#include "probe/mkv_probe.hpp"

#include "gst/gst_raii.hpp"

#include <gst/gst.h>

#include <cstring>

namespace {

struct ProbeState {
    bool hasVideo = false;
    bool hasAudio = false;
    bool videoSupported = false;
    bool done = false;
    VideoCodec videoCodec = VideoCodec::H264;
};

void onPadAdded(GstElement *demux, GstPad *sourcePad, gpointer userData)
{
    ProbeState &state = *static_cast<ProbeState *>(userData);
    GstCapsPtr caps(gst_pad_get_current_caps(sourcePad));
    if (!caps) {
        caps.reset(gst_pad_query_caps(sourcePad, NULL));
    }

    if (caps && !gst_caps_is_empty(caps.get())) {
        const gchar *mediaType =
            gst_structure_get_name(gst_caps_get_structure(caps.get(), 0));
        if (g_str_has_prefix(mediaType, "video/") && !state.hasVideo) {
            state.hasVideo = true;
            if (std::strcmp(mediaType, "video/x-h264") == 0) {
                state.videoSupported = true;
                state.videoCodec = VideoCodec::H264;
            } else if (std::strcmp(mediaType, "video/x-h265") == 0) {
                state.videoSupported = true;
                state.videoCodec = VideoCodec::H265;
            }
        } else if (g_str_has_prefix(mediaType, "audio/")) {
            state.hasAudio = true;
        }
    }

    GstObjectPtr<GstElement> pipeline(
        GST_ELEMENT(gst_element_get_parent(demux)));
    GstObjectPtr<GstElement> sink(gst_element_factory_make("fakesink", NULL));
    if (pipeline && sink) {
        g_object_set(sink.get(), "async", FALSE, "sync", FALSE, NULL);
        if (gst_bin_add(GST_BIN(pipeline.get()), sink.get())) {
            GstElement *ownedSink = sink.release();
            GstObjectPtr<GstPad> sinkPad(
                gst_element_get_static_pad(ownedSink, "sink"));
            gst_pad_link(sourcePad, sinkPad.get());
            gst_element_sync_state_with_parent(ownedSink);
            gst_element_post_message(
                pipeline.get(),
                gst_message_new_application(
                    GST_OBJECT(demux),
                    gst_structure_new_empty("mkv-probe-pad-added")));
        }
    }
}

void onNoMorePads(GstElement *demux, gpointer userData)
{
    ProbeState &state = *static_cast<ProbeState *>(userData);
    state.done = true;
    GstObjectPtr<GstElement> pipeline(
        GST_ELEMENT(gst_element_get_parent(demux)));
    if (pipeline) {
        gst_element_post_message(
            pipeline.get(),
            gst_message_new_application(
                GST_OBJECT(demux), gst_structure_new_empty("mkv-probe-done")));
    }
}

} // namespace

MkvMediaInfo::MkvMediaInfo()
    : hasVideo_(false),
      hasAudio_(false),
      videoCodec_(VideoCodec::H264)
{
}

MkvMediaInfo::MkvMediaInfo(bool hasVideo, bool hasAudio, VideoCodec videoCodec)
    : hasVideo_(hasVideo),
      hasAudio_(hasAudio),
      videoCodec_(videoCodec)
{
}

bool MkvMediaInfo::hasVideo() const { return hasVideo_; }
bool MkvMediaInfo::hasAudio() const { return hasAudio_; }
VideoCodec MkvMediaInfo::videoCodec() const { return videoCodec_; }

bool MkvProbe::inspect(const std::string &path, MkvMediaInfo &mediaInfo) const
{
    GstObjectPtr<GstElement> pipeline(gst_pipeline_new("mkv_probe_pipeline"));
    GstObjectPtr<GstElement> source(
        gst_element_factory_make("filesrc", "mkv_probe_source"));
    GstObjectPtr<GstElement> demux(
        gst_element_factory_make("matroskademux", "mkv_probe_demux"));
    ProbeState state;

    if (!pipeline || !source || !demux) {
        g_printerr("Failed to create elements for probing MKV file\n");
        return false;
    }

    g_object_set(source.get(), "location", path.c_str(), NULL);
    if (!gst_bin_add(GST_BIN(pipeline.get()), source.get())) {
        g_printerr("Failed to add MKV source to probe pipeline\n");
        return false;
    }
    GstElement *ownedSource = source.release();
    if (!gst_bin_add(GST_BIN(pipeline.get()), demux.get())) {
        g_printerr("Failed to add MKV demuxer to probe pipeline\n");
        return false;
    }
    GstElement *ownedDemux = demux.release();

    if (!gst_element_link(ownedSource, ownedDemux)) {
        g_printerr("Failed to link elements for probing MKV file\n");
        return false;
    }
    g_signal_connect(ownedDemux, "pad-added", G_CALLBACK(onPadAdded), &state);
    g_signal_connect(ownedDemux, "no-more-pads", G_CALLBACK(onNoMorePads), &state);

    GstObjectPtr<GstBus> bus(gst_element_get_bus(pipeline.get()));
    const GstStateChangeReturn stateResult =
        gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
    bool probeFailed = stateResult == GST_STATE_CHANGE_FAILURE;
    const gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;

    while (!probeFailed && !state.done) {
        const gint64 remainingMicroseconds = deadline - g_get_monotonic_time();
        if (remainingMicroseconds <= 0) {
            break;
        }
        GstMessagePtr message(gst_bus_timed_pop_filtered(
            bus.get(),
            static_cast<GstClockTime>(remainingMicroseconds) * GST_USECOND,
            static_cast<GstMessageType>(
                GST_MESSAGE_APPLICATION | GST_MESSAGE_ERROR | GST_MESSAGE_EOS)));
        if (!message) {
            break;
        }
        if (GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_ERROR) {
            GError *error = NULL;
            gchar *debugInfo = NULL;
            gst_message_parse_error(message.get(), &error, &debugInfo);
            g_printerr("Failed to inspect MKV file: %s\n",
                       error ? error->message : "unknown GStreamer error");
            if (error) {
                g_error_free(error);
            }
            g_free(debugInfo);
            probeFailed = true;
        }
    }

    gst_element_set_state(pipeline.get(), GST_STATE_NULL);

    if (probeFailed) {
        return false;
    }
    if (!state.hasVideo && !state.hasAudio) {
        g_printerr("Selected MKV file has no supported audio or video streams\n");
        return false;
    }
    if (state.hasVideo && !state.videoSupported) {
        g_printerr("MKV passthrough supports H.264 and H.265 video only\n");
        return false;
    }

    mediaInfo = MkvMediaInfo(state.hasVideo, state.hasAudio, state.videoCodec);
    return true;
}
