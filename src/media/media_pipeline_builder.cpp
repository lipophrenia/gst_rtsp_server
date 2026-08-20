#include "media/media_pipeline_builder.hpp"

#include "config/app_config.hpp"

MediaPipelineBuilder::MediaPipelineBuilder(const AppConfig &config)
    : config_(config)
{
}

MediaPipelineBuilder::~MediaPipelineBuilder()
{
}

const AppConfig &MediaPipelineBuilder::config() const
{
    return config_;
}

const char *MediaPipelineBuilder::parserFactoryName() const
{
    return config_.videoCodec() == VideoCodec::H265 ? "h265parse" : "h264parse";
}

const char *MediaPipelineBuilder::payloaderFactoryName() const
{
    return config_.videoCodec() == VideoCodec::H265 ? "rtph265pay" : "rtph264pay";
}

void MediaPipelineBuilder::configureVideoEncoder(GstElement *encoder) const
{
    if (!config_.useMpp() && config_.lowLatency()) {
        g_object_set(encoder,
                     "speed-preset", 1,
                     "tune", 4,
                     "key-int-max", config_.videoFps(),
                     NULL);
    }
}
