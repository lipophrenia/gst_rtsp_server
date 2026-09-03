#include "media/media_factory.hpp"

#include "config/app_config.hpp"
#include "media/camera_pipeline_builder.hpp"
#include "media/media_pipeline_builder.hpp"
#include "media/mkv_pipeline_builder.hpp"

namespace {

typedef struct _ConfiguredMediaFactory {
    GstRTSPMediaFactory parent;
    const MediaPipelineBuilder *pipelineBuilder;
    gboolean reducedResolution;
} ConfiguredMediaFactory;

typedef struct _ConfiguredMediaFactoryClass {
    GstRTSPMediaFactoryClass parentClass;
} ConfiguredMediaFactoryClass;

G_DEFINE_TYPE(ConfiguredMediaFactory, configured_media_factory,
              GST_TYPE_RTSP_MEDIA_FACTORY)

GstElement *createElement(GstRTSPMediaFactory *factory, const GstRTSPUrl *url)
{
    (void)url;
    ConfiguredMediaFactory *configured =
        reinterpret_cast<ConfiguredMediaFactory *>(factory);
    if (!configured->pipelineBuilder) {
        g_printerr("RTSP media factory has no pipeline builder\n");
        return NULL;
    }
    return configured->pipelineBuilder->build(
        configured->reducedResolution != FALSE);
}

void configured_media_factory_class_init(ConfiguredMediaFactoryClass *klass)
{
    GstRTSPMediaFactoryClass *factoryClass = GST_RTSP_MEDIA_FACTORY_CLASS(klass);
    factoryClass->create_element = createElement;
}

void configured_media_factory_init(ConfiguredMediaFactory *factory)
{
    factory->pipelineBuilder = NULL;
    factory->reducedResolution = FALSE;
}

} // namespace

MediaFactory::MediaFactory(const AppConfig &config)
    : config_(config),
      primaryPipelineBuilder_(config.isMkvSource()
                                  ? static_cast<MediaPipelineBuilder *>(
                                        new MkvPipelineBuilder(config))
                                  : static_cast<MediaPipelineBuilder *>(
                                        new CameraPipelineBuilder(config))),
      subMkvPipelineBuilder_(config.hasSubMkvSource()
                                 ? static_cast<MediaPipelineBuilder *>(
                                       new MkvPipelineBuilder(config))
                                 : NULL)
{
}

MediaFactory::~MediaFactory()
{
}

GstRTSPMediaFactory *MediaFactory::create(bool reducedResolution) const
{
    ConfiguredMediaFactory *configured = static_cast<ConfiguredMediaFactory *>(
        g_object_new(configured_media_factory_get_type(), NULL));
    configured->pipelineBuilder =
        reducedResolution && subMkvPipelineBuilder_
            ? subMkvPipelineBuilder_.get()
            : primaryPipelineBuilder_.get();
    configured->reducedResolution = reducedResolution ? TRUE : FALSE;

    GstRTSPMediaFactory *factory = GST_RTSP_MEDIA_FACTORY(configured);
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    gst_rtsp_media_factory_set_stop_on_disconnect(factory, TRUE);
    gst_rtsp_media_factory_set_protocols(
        factory,
        config_.tcpOnly()
            ? GST_RTSP_LOWER_TRANS_TCP
            : static_cast<GstRTSPLowerTrans>(GST_RTSP_LOWER_TRANS_UDP |
                                             GST_RTSP_LOWER_TRANS_UDP_MCAST |
                                             GST_RTSP_LOWER_TRANS_TCP));
    return factory;
}
