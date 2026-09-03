#include "media/camera_pipeline_builder.hpp"

#include "config/app_config.hpp"
#include "gst/gst_raii.hpp"
#include "media/element_assembly.hpp"

namespace {

struct CameraVideoTimestampNormalizer {
    explicit CameraVideoTimestampNormalizer(guint frameRate)
        : fps(frameRate),
          inputAnchor(GST_CLOCK_TIME_NONE),
          outputAnchor(GST_CLOCK_TIME_NONE),
          lastOutputPts(GST_CLOCK_TIME_NONE),
          lastFrameIndex(0)
    {
    }

    guint fps;
    GstClockTime inputAnchor;
    GstClockTime outputAnchor;
    GstClockTime lastOutputPts;
    guint64 lastFrameIndex;
};

struct CameraAudioTimestampNormalizer {
    explicit CameraAudioTimestampNormalizer(guint audioSampleRate)
        : sampleRate(audioSampleRate),
          outputAnchor(GST_CLOCK_TIME_NONE),
          nextSampleOffset(0)
    {
    }

    guint sampleRate;
    GstClockTime outputAnchor;
    guint64 nextSampleOffset;
};

const guint kCameraAudioSampleRate = 8000;
const gsize kCameraAudioBytesPerSample = sizeof(gint16);

GstClockTime frameOffset(guint64 frameIndex, guint fps)
{
    return gst_util_uint64_scale(frameIndex, GST_SECOND, fps);
}

GstClockTime frameDuration(guint64 frameIndex, guint fps)
{
    return frameOffset(frameIndex + 1, fps) - frameOffset(frameIndex, fps);
}

void resetVideoTimestampNormalizer(CameraVideoTimestampNormalizer &normalizer,
                                   GstClockTime inputPts,
                                   GstClockTime outputPts)
{
    normalizer.inputAnchor = inputPts;
    normalizer.outputAnchor = outputPts;
    normalizer.lastOutputPts = outputPts;
    normalizer.lastFrameIndex = 0;
}

GstPadProbeReturn normalizeCameraVideoTimestamp(GstPad *pad,
                                                GstPadProbeInfo *info,
                                                gpointer userData)
{
    (void)pad;
    CameraVideoTimestampNormalizer &normalizer =
        *static_cast<CameraVideoTimestampNormalizer *>(userData);
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buffer) {
        return GST_PAD_PROBE_OK;
    }

    buffer = gst_buffer_make_writable(buffer);
    if (!buffer) {
        return GST_PAD_PROBE_DROP;
    }
    GST_PAD_PROBE_INFO_DATA(info) = buffer;

    const GstClockTime inputPts = GST_BUFFER_PTS(buffer);
    const bool hasInputPts = GST_CLOCK_TIME_IS_VALID(inputPts);
    const bool isDiscontinuity = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT);

    if (!GST_CLOCK_TIME_IS_VALID(normalizer.outputAnchor)) {
        const GstClockTime firstPts = hasInputPts ? inputPts : 0;
        resetVideoTimestampNormalizer(
            normalizer,
            hasInputPts ? inputPts : GST_CLOCK_TIME_NONE,
            firstPts);
    } else if (isDiscontinuity || !hasInputPts ||
               !GST_CLOCK_TIME_IS_VALID(normalizer.inputAnchor) ||
               inputPts < normalizer.inputAnchor) {
        const GstClockTime nextPts =
            normalizer.lastOutputPts +
            frameDuration(normalizer.lastFrameIndex, normalizer.fps);
        resetVideoTimestampNormalizer(
            normalizer,
            hasInputPts ? inputPts : GST_CLOCK_TIME_NONE,
            nextPts);
    } else {
        const GstClockTime elapsed = inputPts - normalizer.inputAnchor;
        guint64 frameIndex = gst_util_uint64_scale_round(
            elapsed, normalizer.fps, GST_SECOND);
        if (frameIndex <= normalizer.lastFrameIndex) {
            frameIndex = normalizer.lastFrameIndex + 1;
        }
        normalizer.lastFrameIndex = frameIndex;
        normalizer.lastOutputPts =
            normalizer.outputAnchor + frameOffset(frameIndex, normalizer.fps);
    }

    GST_BUFFER_PTS(buffer) = normalizer.lastOutputPts;
    if (GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DTS(buffer))) {
        GST_BUFFER_DTS(buffer) = normalizer.lastOutputPts;
    }
    GST_BUFFER_DURATION(buffer) =
        frameDuration(normalizer.lastFrameIndex, normalizer.fps);
    return GST_PAD_PROBE_OK;
}

void destroyVideoTimestampNormalizer(gpointer userData)
{
    delete static_cast<CameraVideoTimestampNormalizer *>(userData);
}

bool installVideoTimestampNormalizer(GstElement *queue, guint fps)
{
    GstObjectPtr<GstPad> sourcePad(gst_element_get_static_pad(queue, "src"));
    if (!sourcePad) {
        return false;
    }

    CameraVideoTimestampNormalizer *normalizer =
        new CameraVideoTimestampNormalizer(fps);
    const gulong probeId = gst_pad_add_probe(
        sourcePad.get(), GST_PAD_PROBE_TYPE_BUFFER,
        normalizeCameraVideoTimestamp, normalizer,
        destroyVideoTimestampNormalizer);
    if (probeId == 0) {
        delete normalizer;
        return false;
    }
    return true;
}

GstClockTime audioSampleOffset(guint64 sampleOffset, guint sampleRate)
{
    return gst_util_uint64_scale(sampleOffset, GST_SECOND, sampleRate);
}

GstPadProbeReturn normalizeCameraAudioTimestamp(GstPad *pad,
                                                GstPadProbeInfo *info,
                                                gpointer userData)
{
    (void)pad;
    CameraAudioTimestampNormalizer &normalizer =
        *static_cast<CameraAudioTimestampNormalizer *>(userData);
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buffer) {
        return GST_PAD_PROBE_OK;
    }

    const gsize bufferSize = gst_buffer_get_size(buffer);
    if (bufferSize == 0 || bufferSize % kCameraAudioBytesPerSample != 0) {
        return GST_PAD_PROBE_OK;
    }
    const guint64 sampleCount = bufferSize / kCameraAudioBytesPerSample;

    buffer = gst_buffer_make_writable(buffer);
    if (!buffer) {
        return GST_PAD_PROBE_DROP;
    }
    GST_PAD_PROBE_INFO_DATA(info) = buffer;

    const GstClockTime inputPts = GST_BUFFER_PTS(buffer);
    const bool hasInputPts = GST_CLOCK_TIME_IS_VALID(inputPts);
    const bool isDiscontinuity =
        GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT);

    if (!GST_CLOCK_TIME_IS_VALID(normalizer.outputAnchor)) {
        normalizer.outputAnchor = hasInputPts ? inputPts : 0;
        normalizer.nextSampleOffset = 0;
    } else if (isDiscontinuity) {
        const GstClockTime expectedPts =
            normalizer.outputAnchor +
            audioSampleOffset(normalizer.nextSampleOffset,
                              normalizer.sampleRate);
        normalizer.outputAnchor =
            hasInputPts && inputPts > expectedPts ? inputPts : expectedPts;
        normalizer.nextSampleOffset = 0;
    }

    const GstClockTime startOffset =
        audioSampleOffset(normalizer.nextSampleOffset, normalizer.sampleRate);
    const GstClockTime endOffset =
        audioSampleOffset(normalizer.nextSampleOffset + sampleCount,
                          normalizer.sampleRate);
    const GstClockTime outputPts = normalizer.outputAnchor + startOffset;

    GST_BUFFER_PTS(buffer) = outputPts;
    if (GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DTS(buffer))) {
        GST_BUFFER_DTS(buffer) = outputPts;
    }
    GST_BUFFER_DURATION(buffer) = endOffset - startOffset;
    normalizer.nextSampleOffset += sampleCount;
    return GST_PAD_PROBE_OK;
}

void destroyAudioTimestampNormalizer(gpointer userData)
{
    delete static_cast<CameraAudioTimestampNormalizer *>(userData);
}

bool installAudioTimestampNormalizer(GstElement *capsFilter)
{
    GstObjectPtr<GstPad> sourcePad(
        gst_element_get_static_pad(capsFilter, "src"));
    if (!sourcePad) {
        return false;
    }

    CameraAudioTimestampNormalizer *normalizer =
        new CameraAudioTimestampNormalizer(kCameraAudioSampleRate);
    const gulong probeId = gst_pad_add_probe(
        sourcePad.get(), GST_PAD_PROBE_TYPE_BUFFER,
        normalizeCameraAudioTimestamp, normalizer,
        destroyAudioTimestampNormalizer);
    if (probeId == 0) {
        delete normalizer;
        return false;
    }
    return true;
}

} // namespace

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

        if (!installVideoTimestampNormalizer(
                queue, static_cast<guint>(settings.videoFps()))) {
            g_printerr("Failed to install camera PTS normalizer\n");
            return NULL;
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

        if (!installAudioTimestampNormalizer(capsFilter)) {
            g_printerr("Failed to install camera audio PTS normalizer\n");
            return NULL;
        }
    }

    return pipeline.release();
}
