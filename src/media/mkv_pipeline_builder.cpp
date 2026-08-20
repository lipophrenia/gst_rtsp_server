#include "media/mkv_pipeline_builder.hpp"

#include "config/app_config.hpp"
#include "gst/gst_raii.hpp"
#include "media/element_assembly.hpp"

#include <cstring>
#include <string>

namespace {

struct MkvPadTargets {
    GstElement *videoQueue;
    GstElement *audioQueue;
};

struct MkvStreamSpec {
    const std::string &path;
    bool hasVideo;
    bool hasAudio;
    VideoCodec videoCodec;
    bool videoPassthrough;
};

const char *codecName(VideoCodec codec)
{
    return codec == VideoCodec::H265 ? "h265" : "h264";
}

const char *parserForCodec(VideoCodec codec)
{
    return codec == VideoCodec::H265 ? "h265parse" : "h264parse";
}

const char *payloaderForCodec(VideoCodec codec)
{
    return codec == VideoCodec::H265 ? "rtph265pay" : "rtph264pay";
}

void onMkvPadAdded(GstElement *decodebin, GstPad *sourcePad, gpointer userData)
{
    (void)decodebin;
    MkvPadTargets &targets = *static_cast<MkvPadTargets *>(userData);
    GstCapsPtr caps(gst_pad_get_current_caps(sourcePad));
    if (!caps) {
        caps.reset(gst_pad_query_caps(sourcePad, NULL));
    }
    if (!caps || gst_caps_is_empty(caps.get())) {
        return;
    }

    const gchar *mediaType =
        gst_structure_get_name(gst_caps_get_structure(caps.get(), 0));
    GstElement *target = NULL;
    if (g_str_has_prefix(mediaType, "video/x-raw") ||
        std::strcmp(mediaType, "video/x-h264") == 0 ||
        std::strcmp(mediaType, "video/x-h265") == 0) {
        target = targets.videoQueue;
    } else if (g_str_has_prefix(mediaType, "audio/x-raw")) {
        target = targets.audioQueue;
    }

    if (target) {
        GstObjectPtr<GstPad> sinkPad(gst_element_get_static_pad(target, "sink"));
        if (!gst_pad_is_linked(sinkPad.get())) {
            const GstPadLinkReturn result = gst_pad_link(sourcePad, sinkPad.get());
            if (result != GST_PAD_LINK_OK) {
                g_printerr("Failed to link MKV %s stream: %s\n",
                           mediaType, gst_pad_link_get_name(result));
            }
        }
    }
}

} // namespace

MkvPipelineBuilder::MkvPipelineBuilder(const AppConfig &config)
    : MediaPipelineBuilder(config)
{
}

GstElement *MkvPipelineBuilder::build(bool reducedResolution) const
{
    const bool useSeparateSubMkv =
        reducedResolution && config().hasSubMkvSource();
    const MkvStreamSpec stream = useSeparateSubMkv
        ? MkvStreamSpec{config().subMkvPath(),
                        config().subMkvHasVideo(),
                        config().subMkvHasAudio(),
                        config().subMkvVideoCodec(),
                        true}
        : MkvStreamSpec{config().mkvPath(),
                        config().hasVideo(),
                        config().hasAudio(),
                        config().videoCodec(),
                        !reducedResolution};

    ElementAssembly pipeline("mkv_media_bin");
    GstElement *decodebin = pipeline.make("uridecodebin", "mkv_decoder0");
    GError *uriError = NULL;
    GCharPtr uri(gst_filename_to_uri(stream.path.c_str(), &uriError));

    if (!pipeline.valid() || !decodebin || !uri) {
        g_printerr("Failed to create MKV source for %s%s%s\n",
                   stream.path.c_str(),
                   uriError ? ": " : "",
                   uriError ? uriError->message : "");
        if (uriError) {
            g_error_free(uriError);
        }
        return NULL;
    }
    if (uriError) {
        g_error_free(uriError);
    }

    g_object_set(decodebin, "uri", uri.get(), NULL);
    GstCapsPtr decodeCaps(gst_caps_from_string(
        stream.videoPassthrough
            ? "video/x-h264; video/x-h265; audio/x-raw"
            : "video/x-raw; audio/x-raw"));
    g_object_set(decodebin, "caps", decodeCaps.get(), NULL);
    if (!pipeline.add({decodebin})) {
        g_printerr("Failed to add MKV decoder to media bin\n");
        return NULL;
    }

    MkvPadTargets *targets = g_new0(MkvPadTargets, 1);
    g_object_set_data_full(G_OBJECT(decodebin), "mkv-pad-targets", targets, g_free);

    if (stream.hasVideo) {
        GstElement *queue = pipeline.make("queue", "mkv_video_queue");
        GstElement *parser =
            pipeline.make(parserForCodec(stream.videoCodec), "mkv_video_parser0");
        GstElement *pay =
            pipeline.make(payloaderForCodec(stream.videoCodec), "pay0");

        if (!queue || !parser || !pay) {
            g_printerr("Failed to create MKV %s passthrough elements (%s, %s)\n",
                       codecName(stream.videoCodec),
                       parserForCodec(stream.videoCodec),
                       payloaderForCodec(stream.videoCodec));
            return NULL;
        }
        g_object_set(pay,
                     "pt", config().videoPayloadType(),
                     "config-interval", 1,
                     NULL);

        if (!stream.videoPassthrough) {
            GstElement *convert =
                pipeline.make("videoconvert", "mkv_low_videoconvert0");
            GstElement *scale =
                pipeline.make("videoscale", "mkv_low_videoscale0");
            GstElement *capsFilter =
                pipeline.make("capsfilter", "mkv_low_video_caps");
            GstElement *encoder =
                pipeline.make(config().videoEncoderFactoryName(),
                              "mkv_low_video_encoder0");

            if (!convert || !scale || !capsFilter || !encoder) {
                g_printerr("Failed to create reduced MKV video elements (%s)\n",
                           config().videoEncoderFactoryName());
                return NULL;
            }

            GstCapsPtr caps(gst_caps_new_simple(
                "video/x-raw",
                "format", G_TYPE_STRING, config().videoEncoderRawFormat(),
                "width", G_TYPE_INT, config().secondaryWidth(),
                "height", G_TYPE_INT, config().secondaryHeight(),
                NULL));
            g_object_set(scale, "add-borders", TRUE, NULL);
            g_object_set(capsFilter, "caps", caps.get(), NULL);
            configureVideoEncoder(encoder);

            if (!pipeline.add({queue, convert, scale, capsFilter,
                               encoder, parser, pay}) ||
                !gst_element_link_many(queue, convert, scale, capsFilter,
                                       encoder, parser, pay, NULL)) {
                g_printerr("Failed to assemble reduced MKV %s video chain\n",
                           codecName(stream.videoCodec));
                return NULL;
            }
        } else {
            if (!pipeline.add({queue, parser, pay}) ||
                !gst_element_link_many(queue, parser, pay, NULL)) {
                g_printerr("Failed to assemble MKV %s passthrough chain\n",
                           codecName(stream.videoCodec));
                return NULL;
            }
        }
        targets->videoQueue = queue;
    }

    if (stream.hasAudio) {
        const char *audioPayName = stream.hasVideo ? "pay1" : "pay0";
        GstElement *queue = pipeline.make("queue", "mkv_audio_queue");
        GstElement *convert =
            pipeline.make("audioconvert", "mkv_audioconvert0");
        GstElement *resample =
            pipeline.make("audioresample", "mkv_audioresample0");
        GstElement *capsFilter =
            pipeline.make("capsfilter", "mkv_audio_caps");
        GstElement *alawEncoder = pipeline.make("alawenc", "mkv_alawenc0");
        GstElement *pay = pipeline.make("rtppcmapay", audioPayName);

        if (!queue || !convert || !resample || !capsFilter || !alawEncoder || !pay) {
            g_printerr("Failed to create MKV audio elements\n");
            return NULL;
        }

        GstCapsPtr caps(gst_caps_new_simple(
            "audio/x-raw",
            "format", G_TYPE_STRING, "S16LE",
            "rate", G_TYPE_INT, 8000,
            "channels", G_TYPE_INT, 1,
            NULL));
        g_object_set(capsFilter, "caps", caps.get(), NULL);
        g_object_set(pay, "pt", config().audioPayloadType(), NULL);

        if (!pipeline.add({queue, convert, resample,
                           capsFilter, alawEncoder, pay}) ||
            !gst_element_link_many(queue, convert, resample,
                                   capsFilter, alawEncoder, pay, NULL)) {
            g_printerr("Failed to assemble MKV audio chain\n");
            return NULL;
        }
        targets->audioQueue = queue;
    }

    g_signal_connect(decodebin, "pad-added", G_CALLBACK(onMkvPadAdded), targets);
    return pipeline.release();
}
