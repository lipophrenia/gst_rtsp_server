#pragma once

#include "media/media_pipeline_builder.hpp"

class MkvPipelineBuilder final : public MediaPipelineBuilder {
public:
    explicit MkvPipelineBuilder(const AppConfig &config);
    GstElement *build(bool reducedResolution) const override;
};
