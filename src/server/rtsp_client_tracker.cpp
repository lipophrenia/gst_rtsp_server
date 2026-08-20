#include "server/rtsp_client_tracker.hpp"

#include "config/app_config.hpp"

#include <cstring>

namespace {

const char *const kClientMountDataKey = "rtsp-client-selected-mount";

} // namespace

RtspClientTracker::RtspClientTracker(const AppConfig &config)
    : config_(config)
{
}

void RtspClientTracker::attach(GstRTSPClient *client)
{
    g_print("RTSP client %p connected (ip=%s, mount=pending)\n",
            static_cast<void *>(client), clientIp(client));

    g_signal_connect(client, "describe-request", G_CALLBACK(onRequest), this);
    g_signal_connect(client, "setup-request", G_CALLBACK(onRequest), this);
    g_signal_connect(client, "play-request", G_CALLBACK(onRequest), this);
    g_signal_connect(client, "closed", G_CALLBACK(onClosed), this);
}

void RtspClientTracker::onRequest(GstRTSPClient *client,
                                  GstRTSPContext *context,
                                  gpointer userData)
{
    RtspClientTracker &tracker = *static_cast<RtspClientTracker *>(userData);
    const char *requestPath =
        context && context->uri ? context->uri->abspath : NULL;
    const char *mountPath = tracker.mountForPath(requestPath);
    if (!mountPath) {
        return;
    }

    const char *previousMount = static_cast<const char *>(
        g_object_get_data(G_OBJECT(client), kClientMountDataKey));
    if (previousMount && std::strcmp(previousMount, mountPath) == 0) {
        return;
    }

    g_object_set_data_full(G_OBJECT(client), kClientMountDataKey,
                           g_strdup(mountPath), g_free);
    g_print("RTSP client %p selected mount=%s (ip=%s, request=%s)\n",
            static_cast<void *>(client), mountPath, clientIp(client),
            requestPath ? requestPath : "unknown");
}

void RtspClientTracker::onClosed(GstRTSPClient *client, gpointer userData)
{
    (void)userData;
    const char *mountPath = static_cast<const char *>(
        g_object_get_data(G_OBJECT(client), kClientMountDataKey));
    g_print("RTSP client %p disconnected (ip=%s, mount=%s)\n",
            static_cast<void *>(client), clientIp(client),
            mountPath ? mountPath : "unknown");
}

const char *RtspClientTracker::mountForPath(const char *requestPath) const
{
    const bool secondaryAvailable = config_.secondaryStreamAvailable();
    const bool matchesPrimary =
        pathBelongsToMount(requestPath, config_.mountPath().c_str());
    const bool matchesSecondary =
        secondaryAvailable &&
        pathBelongsToMount(requestPath, config_.secondaryMountPath().c_str());

    if (matchesPrimary && matchesSecondary) {
        return config_.secondaryMountPath().size() > config_.mountPath().size()
                   ? config_.secondaryMountPath().c_str()
                   : config_.mountPath().c_str();
    }
    if (matchesSecondary) {
        return config_.secondaryMountPath().c_str();
    }
    if (matchesPrimary) {
        return config_.mountPath().c_str();
    }
    return NULL;
}

bool RtspClientTracker::pathBelongsToMount(const char *requestPath,
                                           const char *mountPath)
{
    if (!requestPath || !mountPath) {
        return false;
    }
    const std::size_t mountLength = std::strlen(mountPath);
    return std::strncmp(requestPath, mountPath, mountLength) == 0 &&
           (requestPath[mountLength] == '\0' || requestPath[mountLength] == '/');
}

const char *RtspClientTracker::clientIp(GstRTSPClient *client)
{
    GstRTSPConnection *connection = gst_rtsp_client_get_connection(client);
    const gchar *ip = connection ? gst_rtsp_connection_get_ip(connection) : NULL;
    return ip ? ip : "unknown";
}
