#pragma once

#include <gst/gst.h>

class AppConfig;

class MediaPipelineBuilder {
public:
    explicit MediaPipelineBuilder(const AppConfig &config);
    virtual ~MediaPipelineBuilder();

    MediaPipelineBuilder(const MediaPipelineBuilder &) = delete;
    MediaPipelineBuilder &operator=(const MediaPipelineBuilder &) = delete;

    virtual GstElement *build(bool reducedResolution) const = 0;

protected:
    const AppConfig &config() const;
    const char *parserFactoryName() const;
    const char *payloaderFactoryName() const;
    void configureVideoEncoder(GstElement *encoder) const;

private:
    const AppConfig &config_;
};
