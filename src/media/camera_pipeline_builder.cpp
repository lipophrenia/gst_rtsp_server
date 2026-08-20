#include "media/camera_pipeline_builder.hpp"

#include "config/app_config.hpp"
#include "gst/gst_raii.hpp"
#include "media/element_assembly.hpp"

CameraPipelineBuilder::CameraPipelineBuilder(const AppConfig &config)
    : MediaPipelineBuilder(config)
{
}

GstElement *CameraPipelineBuilder::build(bool reducedResolution) const
{
    ElementAssembly pipeline("media_bin");
    if (!pipeline.valid()) {
        g_printerr("Failed to create media bin\n");
        return NULL;
    }

    const AppConfig &settings = config();

    if (settings.hasVideo()) {
        const char *encoderFactory = settings.videoEncoderFactoryName();
        const bool needsConvert = reducedResolution || !settings.useMpp();

        GstElement *source = pipeline.make("v4l2src", "v4l2src0");
        GstElement *sourceCaps = pipeline.make("capsfilter", "v_caps");
        GstElement *queue = pipeline.make("queue", "v_queue");
        GstElement *convert = needsConvert
                                  ? pipeline.make("videoconvert", "video_convert0")
                                  : NULL;
        GstElement *scale = reducedResolution
                                ? pipeline.make("videoscale", "low_videoscale0")
                                : NULL;
        GstElement *lowCaps = reducedResolution
                                  ? pipeline.make("capsfilter", "low_video_caps")
                                  : NULL;
        GstElement *encoder = pipeline.make(encoderFactory, "video_encoder0");
        GstElement *parser = pipeline.make(parserFactoryName(), "video_parser0");
        GstElement *pay = pipeline.make(payloaderFactoryName(), "pay0");

        if (!source || !sourceCaps || !queue || !encoder || !parser || !pay ||
            (needsConvert && !convert) ||
            (reducedResolution && (!scale || !lowCaps))) {
            g_printerr("Failed to create %s video elements (%s, %s, %s)\n",
                       settings.videoCodecName(), encoderFactory,
                       parserFactoryName(), payloaderFactoryName());
            return NULL;
        }

        GstCapsPtr caps(gst_caps_new_simple(
            "video/x-raw",
            "width", G_TYPE_INT, settings.videoWidth(),
            "height", G_TYPE_INT, settings.videoHeight(),
            "framerate", GST_TYPE_FRACTION, settings.videoFps(), 1,
            NULL));
        g_object_set(source,
                     "device", settings.videoDevice().c_str(),
                     "io-mode", 5,
                     "do-timestamp", TRUE,
                     NULL);
        g_object_set(sourceCaps, "caps", caps.get(), NULL);
        g_object_set(pay,
                     "pt", settings.videoPayloadType(),
                     "config-interval", 1,
                     NULL);
        configureVideoEncoder(encoder);

        if (reducedResolution) {
            GstCapsPtr reducedCaps(gst_caps_new_simple(
                "video/x-raw",
                "format", G_TYPE_STRING, settings.videoEncoderRawFormat(),
                "width", G_TYPE_INT, settings.secondaryWidth(),
                "height", G_TYPE_INT, settings.secondaryHeight(),
                NULL));
            g_object_set(scale, "add-borders", TRUE, NULL);
            g_object_set(lowCaps, "caps", reducedCaps.get(), NULL);
        }

        if (settings.lowLatency()) {
            g_object_set(queue,
                         "max-size-buffers", 4,
                         "max-size-bytes", 0,
                         "max-size-time", static_cast<guint64>(0),
                         "leaky", 2,
                         NULL);
        }

        if (reducedResolution) {
            if (!pipeline.add({source, sourceCaps, queue, convert, scale, lowCaps,
                               encoder, parser, pay}) ||
                !gst_element_link_many(source, sourceCaps, queue, convert, scale,
                                       lowCaps, encoder, parser, pay, NULL)) {
                g_printerr("Failed to assemble reduced %s camera video chain\n",
                           settings.videoCodecName());
                return NULL;
            }
        } else if (needsConvert) {
            if (!pipeline.add({source, sourceCaps, queue, convert, encoder, parser, pay}) ||
                !gst_element_link_many(source, sourceCaps, queue, convert,
                                       encoder, parser, pay, NULL)) {
                g_printerr("Failed to assemble %s camera video chain\n",
                           settings.videoCodecName());
                return NULL;
            }
        } else {
            if (!pipeline.add({source, sourceCaps, queue, encoder, parser, pay}) ||
                !gst_element_link_many(source, sourceCaps, queue,
                                       encoder, parser, pay, NULL)) {
                g_printerr("Failed to assemble %s camera video chain\n",
                           settings.videoCodecName());
                return NULL;
            }
        }
    }

    if (settings.hasAudio()) {
        GstElement *source = pipeline.make("alsasrc", "alsasrc0");
        GstElement *queue = pipeline.make("queue", "a_queue");
        GstElement *convert = pipeline.make("audioconvert", "audioconvert0");
        GstElement *resample = pipeline.make("audioresample", "audioresample0");
        GstElement *capsFilter = pipeline.make("capsfilter", "a_caps");
        GstElement *alawEncoder = pipeline.make("alawenc", "alawenc0");
        const char *audioPayName =
            settings.streamMode() == StreamMode::Audio ? "pay0" : "pay1";
        GstElement *pay = pipeline.make("rtppcmapay", audioPayName);

        if (!source || !queue || !convert || !resample ||
            !capsFilter || !alawEncoder || !pay) {
            g_printerr("Failed to create audio elements\n");
            return NULL;
        }

        GstCapsPtr caps(gst_caps_new_simple(
            "audio/x-raw",
            "format", G_TYPE_STRING, "S16LE",
            "rate", G_TYPE_INT, 8000,
            "channels", G_TYPE_INT, 1,
            NULL));
        g_object_set(source,
                     "device", settings.audioDevice().c_str(),
                     "do-timestamp", TRUE,
                     NULL);
        g_object_set(capsFilter, "caps", caps.get(), NULL);
        g_object_set(pay, "pt", settings.audioPayloadType(), NULL);

        if (settings.lowLatency()) {
            g_object_set(queue,
                         "max-size-buffers", 16,
                         "max-size-bytes", 0,
                         "max-size-time", static_cast<guint64>(0),
                         "leaky", 2,
                         NULL);
        }

        if (!pipeline.add({source, queue, convert, resample,
                           capsFilter, alawEncoder, pay}) ||
            !gst_element_link_many(source, queue, convert, resample,
                                   capsFilter, alawEncoder, pay, NULL)) {
            g_printerr("Failed to assemble audio chain\n");
            return NULL;
        }
    }

    return pipeline.release();
}
