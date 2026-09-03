#include "server/rtsp_server_app.hpp"

#include "gst/gst_raii.hpp"

#include <utility>

RtspServerApp::RtspServerApp(AppConfig config)
    : config_(std::move(config)),
      clientTracker_(config_),
      mediaFactory_(config_)
{
}

void RtspServerApp::onClientConnected(GstRTSPServer *server,
                                      GstRTSPClient *client,
                                      gpointer userData)
{
    (void)server;
    RtspServerApp *app = static_cast<RtspServerApp *>(userData);
    app->clientTracker_.attach(client);
}

int RtspServerApp::run()
{
    GMainLoopPtr loop(g_main_loop_new(NULL, FALSE));
    GObjectPtr<GstRTSPServer> server(gst_rtsp_server_new());
    if (!loop || !server) {
        g_printerr("Failed to initialize RTSP server\n");
        return 1;
    }

    g_signal_connect(server.get(), "client-connected",
                     G_CALLBACK(onClientConnected), this);

    char port[16];
    g_snprintf(port, sizeof(port), "%u", config_.port());
    gst_rtsp_server_set_service(server.get(), port);

    GObjectPtr<GstRTSPMountPoints> mounts(
        gst_rtsp_server_get_mount_points(server.get()));
    if (!mounts) {
        g_printerr("Failed to get RTSP mount points\n");
        return 1;
    }

    gst_rtsp_mount_points_add_factory(
        mounts.get(), config_.mountPath().c_str(), mediaFactory_.create(false));

    if (config_.secondaryStreamEnabled() &&
        !config_.isMkvSource() && !config_.hasSubMkvSource()) {
        g_printerr("Warning: --sub-resize is ignored in camera mode because the camera "
                   "cannot be opened by two pipelines\n");
    }
    const bool hasSecondaryStream = config_.secondaryStreamAvailable();
    if (hasSecondaryStream) {
        gst_rtsp_mount_points_add_factory(
            mounts.get(), config_.secondaryMountPath().c_str(),
            mediaFactory_.create(true));
    }
    mounts.reset();

    const guint sourceId = gst_rtsp_server_attach(server.get(), NULL);
    if (sourceId == 0) {
        g_printerr("Failed to attach RTSP server\n");
        return 1;
    }

    printEndpoints(hasSecondaryStream);
    g_main_loop_run(loop.get());
    g_source_remove(sourceId);
    return 0;
}

void RtspServerApp::printEndpoints(bool hasSecondaryStream) const
{
    switch (config_.streamMode()) {
        case StreamMode::Video:
            g_print("RTSP %s video only%s: rtsp://%s:%u%s\n",
                    config_.videoCodecName(),
                    config_.isMkvSource() ? " (MKV passthrough)" : "",
                    config_.host().c_str(), config_.port(),
                    config_.mountPath().c_str());
            break;
        case StreamMode::Audio:
            g_print("RTSP audio only%s: rtsp://%s:%u%s\n",
                    config_.isMkvSource() ? " (MKV source)" : "",
                    config_.host().c_str(), config_.port(),
                    config_.mountPath().c_str());
            break;
        case StreamMode::Both:
            g_print("RTSP %s video+audio%s: rtsp://%s:%u%s\n",
                    config_.videoCodecName(),
                    config_.isMkvSource() ? " (MKV video passthrough)" : "",
                    config_.host().c_str(), config_.port(),
                    config_.mountPath().c_str());
            break;
    }

    if (hasSecondaryStream) {
        if (config_.hasSubMkvSource()) {
            if (config_.subMkvHasVideo()) {
                g_print("RTSP %s sub MKV video%s passthrough: rtsp://%s:%u%s\n",
                        config_.subMkvVideoCodecName(),
                        config_.subMkvHasAudio() ? "+audio" : "",
                        config_.host().c_str(), config_.port(),
                        config_.secondaryMountPath().c_str());
            } else {
                g_print("RTSP sub MKV audio only: rtsp://%s:%u%s\n",
                        config_.host().c_str(), config_.port(),
                        config_.secondaryMountPath().c_str());
            }
        } else {
            g_print("RTSP %s reduced video %dx%d%s: rtsp://%s:%u%s\n",
                    config_.videoCodecName(),
                    config_.secondaryWidth(), config_.secondaryHeight(),
                    config_.streamMode() == StreamMode::Both ? "+audio" : "",
                    config_.host().c_str(), config_.port(),
                    config_.secondaryMountPath().c_str());
        }
    }
    if (config_.hasVideo() &&
        (!config_.isMkvSource() ||
         (hasSecondaryStream && !config_.hasSubMkvSource()))) {
        g_print("Video encoder%s: %s\n",
                config_.isMkvSource() ? " for reduced stream" : "",
                config_.videoEncoderFactoryName());
    }
    g_print("RTP transport: %s\n",
            config_.tcpOnly() ? "TCP only" : "UDP, UDP multicast, TCP");
}
