#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

int main(int argc, char *argv[]) {
    GMainLoop *loop;
    GstRTSPServer *server;
    GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *factory1;
    GstRTSPMediaFactory *factory2;

    gst_init(&argc, &argv);

    loop = g_main_loop_new(NULL, FALSE);

    server = gst_rtsp_server_new();
    gst_rtsp_server_set_service(server, "8554");
    mounts = gst_rtsp_server_get_mount_points(server);

    //1st stream
    factory1 = gst_rtsp_media_factory_new();
        gst_rtsp_media_factory_set_launch(factory1,
        "("
        " v4l2src io-mode=5 ! video/x-raw,width=1280,height=800 ! mpph264enc ! h264parse ! rtph264pay name=pay0 pt=96 "

        " audiotestsrc is-live=true wave=sine ! audioconvert ! audioresample ! audio/x-raw,format=S16LE,rate=8000,channels=1 ! alawenc ! rtppcmapay name=pay1 pt=8 "
        ")");
    // gst_rtsp_media_factory_set_launch(factory1,
        // "(v4l2src io-mode=5 ! video/x-raw,width=1280,height=800 ! mpph264enc ! h264parse ! rtph264pay name=pay0 pt=96 zero-latency)");
    // "(rtspsrc location=rtsp://192.168.0.8:8554/restream ! rtph264depay ! h264parse ! mppvideodec ! mpph264enc ! h264parse ! rtph264pay name=pay0 pt=96 zero-latency)");
    gst_rtsp_media_factory_set_shared(factory1, TRUE);
    gst_rtsp_media_factory_set_suspend_mode(factory1, GST_RTSP_SUSPEND_MODE_NONE);
    gst_rtsp_mount_points_add_factory(mounts, "/stream1", factory1);

    //2nd stream
    // factory2 = gst_rtsp_media_factory_new();
    // gst_rtsp_media_factory_set_launch(factory2,
    // "(v4l2src device=/dev/video2 io-mode=5 ! video/x-raw, width=384, height=288, framerate=50/1 ! mpph264enc ! h264parse ! rtph264pay name=pay0 pt=64 zero-latency)");
    // gst_rtsp_media_factory_set_shared(factory2, TRUE);
    // gst_rtsp_mount_points_add_factory(mounts, "/stream2", factory2);

    g_object_unref(mounts);
    gst_rtsp_server_attach(server, NULL);

    g_print("rtsp://192.168.0.5:8554/stream1 is live\n");
    // g_print("rtsp://192.168.0.5:554/stream2 is live\n");

    g_main_loop_run(loop);

    return 0;
}
