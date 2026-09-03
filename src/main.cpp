#include "config/app_config.hpp"
#include "probe/mkv_probe.hpp"
#include "server/rtsp_server_app.hpp"

#include <gst/gst.h>

#include <utility>

int main(int argc, char *argv[])
{
    AppConfig config;
    config.parse(argc, argv);

    gst_init(&argc, &argv);
    if (config.isMkvSource() || config.hasSubMkvSource()) {
        MkvProbe probe;
        if (config.isMkvSource()) {
            MkvMediaInfo mediaInfo;
            if (!probe.inspect(config.mkvPath(), mediaInfo)) {
                return 1;
            }
            config.applyMkvMediaInfo(
                mediaInfo.hasVideo(), mediaInfo.hasAudio(), mediaInfo.videoCodec());
        }

        if (config.hasSubMkvSource()) {
            MkvMediaInfo subMediaInfo;
            if (!probe.inspect(config.subMkvPath(), subMediaInfo)) {
                return 1;
            }
            config.applySubMkvMediaInfo(
                subMediaInfo.hasVideo(),
                subMediaInfo.hasAudio(),
                subMediaInfo.videoCodec());
        }
    }
    if (config.quietRtspClientLogs()) {
        gst_debug_set_threshold_for_name("rtspclient", GST_LEVEL_NONE);
    }

    RtspServerApp server(std::move(config));
    return server.run();
}
