#pragma once

#include "config/app_config.hpp"
#include "media/media_factory.hpp"
#include "server/rtsp_client_tracker.hpp"

#include <gst/rtsp-server/rtsp-server.h>

class RtspServerApp final {
public:
    explicit RtspServerApp(AppConfig config);

    RtspServerApp(const RtspServerApp &) = delete;
    RtspServerApp &operator=(const RtspServerApp &) = delete;

    int run();

private:
    static void onClientConnected(GstRTSPServer *server,
                                  GstRTSPClient *client,
                                  gpointer userData);
    void printEndpoints(bool hasSecondaryStream) const;

    AppConfig config_;
    RtspClientTracker clientTracker_;
    MediaFactory mediaFactory_;
};
