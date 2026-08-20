#pragma once

#include <gst/rtsp-server/rtsp-server.h>

class AppConfig;

class RtspClientTracker final {
public:
    explicit RtspClientTracker(const AppConfig &config);

    RtspClientTracker(const RtspClientTracker &) = delete;
    RtspClientTracker &operator=(const RtspClientTracker &) = delete;

    void attach(GstRTSPClient *client);

private:
    static void onRequest(GstRTSPClient *client,
                          GstRTSPContext *context,
                          gpointer userData);
    static void onClosed(GstRTSPClient *client, gpointer userData);

    const char *mountForPath(const char *requestPath) const;
    static bool pathBelongsToMount(const char *requestPath,
                                   const char *mountPath);
    static const char *clientIp(GstRTSPClient *client);

    const AppConfig &config_;
};
