#pragma once

#include <gst/rtsp-server/rtsp-server.h>

#include <memory>

class AppConfig;
class MediaPipelineBuilder;

class MediaFactory final {
public:
    explicit MediaFactory(const AppConfig &config);
    ~MediaFactory();

    MediaFactory(const MediaFactory &) = delete;
    MediaFactory &operator=(const MediaFactory &) = delete;

    GstRTSPMediaFactory *create(bool reducedResolution) const;

private:
    const AppConfig &config_;
    std::unique_ptr<MediaPipelineBuilder> primaryPipelineBuilder_;
    std::unique_ptr<MediaPipelineBuilder> subMkvPipelineBuilder_;
};
