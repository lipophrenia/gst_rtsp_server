#pragma once

#include "media/media_pipeline_builder.hpp"

class CameraPipelineBuilder final : public MediaPipelineBuilder {
public:
    explicit CameraPipelineBuilder(const AppConfig &config);
    GstElement *build(bool reducedResolution) const override;
};
