#include "config/app_config.hpp"

#include <glib.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

bool AppConfig::isMkvSource() const
{
    return !mkvPath_.empty();
}

bool AppConfig::hasVideo() const
{
    return streamMode_ == StreamMode::Video || streamMode_ == StreamMode::Both;
}

bool AppConfig::hasAudio() const
{
    return streamMode_ == StreamMode::Audio || streamMode_ == StreamMode::Both;
}

bool AppConfig::hasSubMkvSource() const { return !subMkvPath_.empty(); }
bool AppConfig::subMkvHasVideo() const { return subMkvHasVideo_; }
bool AppConfig::subMkvHasAudio() const { return subMkvHasAudio_; }

bool AppConfig::secondaryStreamAvailable() const
{
    if (!secondaryStreamEnabled_ || !isMkvSource()) {
        return false;
    }
    return hasSubMkvSource()
               ? (subMkvHasVideo_ || subMkvHasAudio_)
               : hasVideo();
}

void AppConfig::applyMkvMediaInfo(bool hasVideo, bool hasAudio, VideoCodec codec)
{
    if (hasVideo) {
        videoCodec_ = codec;
    }
    streamMode_ = hasVideo && hasAudio
                      ? StreamMode::Both
                      : (hasVideo ? StreamMode::Video : StreamMode::Audio);
}

void AppConfig::applySubMkvMediaInfo(bool hasVideo, bool hasAudio, VideoCodec codec)
{
    subMkvHasVideo_ = hasVideo;
    subMkvHasAudio_ = hasAudio;
    if (hasVideo) {
        subMkvVideoCodec_ = codec;
    }
}

StreamMode AppConfig::streamMode() const { return streamMode_; }
VideoCodec AppConfig::videoCodec() const { return videoCodec_; }
VideoCodec AppConfig::subMkvVideoCodec() const { return subMkvVideoCodec_; }
const std::string &AppConfig::host() const { return host_; }
const std::string &AppConfig::mountPath() const { return mountPath_; }
const std::string &AppConfig::secondaryMountPath() const { return secondaryMountPath_; }
const std::string &AppConfig::videoDevice() const { return videoDevice_; }
const std::string &AppConfig::audioDevice() const { return audioDevice_; }
const std::string &AppConfig::mkvPath() const { return mkvPath_; }
const std::string &AppConfig::subMkvPath() const { return subMkvPath_; }
unsigned int AppConfig::port() const { return port_; }
int AppConfig::videoWidth() const { return videoWidth_; }
int AppConfig::videoHeight() const { return videoHeight_; }
int AppConfig::videoFps() const { return videoFps_; }
int AppConfig::secondaryWidth() const { return secondaryWidth_; }
int AppConfig::secondaryHeight() const { return secondaryHeight_; }
unsigned int AppConfig::videoPayloadType() const { return videoPayloadType_; }
unsigned int AppConfig::audioPayloadType() const { return audioPayloadType_; }
bool AppConfig::lowLatency() const { return lowLatency_; }
bool AppConfig::quietRtspClientLogs() const { return quietRtspClientLogs_; }
bool AppConfig::tcpOnly() const { return tcpOnly_; }
bool AppConfig::useMpp() const { return useMpp_; }
bool AppConfig::secondaryStreamEnabled() const { return secondaryStreamEnabled_; }

const char *AppConfig::videoCodecName() const
{
    return videoCodec_ == VideoCodec::H265 ? "h265" : "h264";
}

const char *AppConfig::videoEncoderFactoryName() const
{
    if (useMpp_) {
        return videoCodec_ == VideoCodec::H265 ? "mpph265enc" : "mpph264enc";
    }
    return videoCodec_ == VideoCodec::H265 ? "x265enc" : "x264enc";
}

const char *AppConfig::videoEncoderRawFormat() const
{
    return useMpp_ ? "NV12" : "I420";
}

const char *AppConfig::subMkvVideoCodecName() const
{
    return subMkvVideoCodec_ == VideoCodec::H265 ? "h265" : "h264";
}

bool AppConfig::parseInt(const char *value, int minValue, int maxValue, int &result)
{
    char *end = NULL;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed < minValue || parsed > maxValue) {
        return false;
    }
    result = static_cast<int>(parsed);
    return true;
}

bool AppConfig::parseUnsigned(const char *value, unsigned int minValue,
                              unsigned int maxValue, unsigned int &result)
{
    char *end = NULL;
    errno = 0;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed < minValue || parsed > maxValue) {
        return false;
    }
    result = static_cast<unsigned int>(parsed);
    return true;
}

std::string AppConfig::resolveMkvPath(const char *value)
{
    gchar *cwd = g_get_current_dir();
    gchar *mkvDirectory = g_build_filename(cwd, "mkv_files", NULL);
    gchar *candidate = NULL;

    if (g_path_is_absolute(value)) {
        candidate = g_strdup(value);
    } else if (g_str_has_prefix(value, "mkv_files/") ||
               g_str_has_prefix(value, "mkv_files\\")) {
        candidate = g_build_filename(cwd, value, NULL);
    } else {
        candidate = g_build_filename(mkvDirectory, value, NULL);
    }

    char *resolvedDirectory = realpath(mkvDirectory, NULL);
    char *resolvedFile = realpath(candidate, NULL);
    g_free(candidate);
    g_free(mkvDirectory);
    g_free(cwd);

    if (!resolvedDirectory || !resolvedFile) {
        g_printerr("MKV file not found: %s\n", value);
        std::free(resolvedDirectory);
        std::free(resolvedFile);
        return std::string();
    }

    const size_t directoryLength = std::strlen(resolvedDirectory);
    const bool insideMkvDirectory =
        std::strncmp(resolvedFile, resolvedDirectory, directoryLength) == 0 &&
        resolvedFile[directoryLength] == G_DIR_SEPARATOR;
    const char *extension = std::strrchr(resolvedFile, '.');
    const bool isMkv = extension && g_ascii_strcasecmp(extension, ".mkv") == 0;

    if (!insideMkvDirectory || !isMkv ||
        !g_file_test(resolvedFile, G_FILE_TEST_IS_REGULAR)) {
        g_printerr("--mkv must point to an .mkv file inside ./mkv_files: %s\n", value);
        std::free(resolvedDirectory);
        std::free(resolvedFile);
        return std::string();
    }

    const std::string result(resolvedFile);
    std::free(resolvedDirectory);
    std::free(resolvedFile);
    return result;
}

void AppConfig::printUsage(const char *programName)
{
    g_print("Usage:\n");
    g_print("  %s                    # video + audio\n", programName);
    g_print("  %s --both             # video + audio\n", programName);
    g_print("  %s --video            # video only\n", programName);
    g_print("  %s --audio            # audio only\n", programName);
    g_print("  %s --port 8554 --mount /stream1 --host 0.0.0.0\n", programName);
    g_print("  %s --sub-resize --secondary-mount /stream-low --secondary-width 640 --secondary-height 360\n",
            programName);
    g_print("  %s --width 1280 --height 800 --fps 30\n", programName);
    g_print("  %s --codec h264|h265  # camera mode only; H.264 by default\n", programName);
    g_print("  %s --mpp              # force mpph264enc/mpph265enc instead of x264enc/x265enc\n",
            programName);
    g_print("  %s --mkv FILE         # auto-detect tracks from FILE in ./mkv_files\n", programName);
    g_print("  %s --mkv FILE --sub-mkv FILE  # use a second MKV as passthrough sub stream\n",
            programName);
    g_print("  %s --mkv 0391_53_50.mkv\n", programName);
    g_print("  %s --video-device /dev/video0 --audio-device plughw:CARD=...,DEV=0\n",
            programName);
    g_print("  %s --sub-resize       # enable the resized secondary MKV stream\n", programName);
    g_print("  %s --low-latency|--no-low-latency\n", programName);
    g_print("  %s --tcp-only         # allow RTP over RTSP/TCP only\n", programName);
    g_print("  %s --quiet-rtspclient-logs\n", programName);
}

void AppConfig::parse(int argc, char *argv[])
{
    int integerValue = 0;
    unsigned int unsignedValue = 0;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--video") == 0) {
            streamMode_ = StreamMode::Video;
        } else if (std::strcmp(argv[i], "--audio") == 0) {
            streamMode_ = StreamMode::Audio;
        } else if (std::strcmp(argv[i], "--both") == 0) {
            streamMode_ = StreamMode::Both;
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            if (!parseUnsigned(argv[++i], 1, 65535, unsignedValue)) {
                g_printerr("Invalid --port value\n");
                std::exit(1);
            }
            port_ = unsignedValue;
        } else if (std::strcmp(argv[i], "--mount") == 0 && i + 1 < argc) {
            mountPath_ = argv[++i];
            if (mountPath_.empty() || mountPath_[0] != '/') {
                g_printerr("--mount must start with '/'\n");
                std::exit(1);
            }
        } else if (std::strcmp(argv[i], "--secondary-mount") == 0 && i + 1 < argc) {
            secondaryMountPath_ = argv[++i];
            if (secondaryMountPath_.empty() || secondaryMountPath_[0] != '/') {
                g_printerr("--secondary-mount must start with '/'\n");
                std::exit(1);
            }
        } else if (std::strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host_ = argv[++i];
        } else if (std::strcmp(argv[i], "--video-device") == 0 && i + 1 < argc) {
            videoDevice_ = argv[++i];
        } else if (std::strcmp(argv[i], "--audio-device") == 0 && i + 1 < argc) {
            audioDevice_ = argv[++i];
        } else if (std::strcmp(argv[i], "--mkv") == 0) {
            if (i + 1 >= argc) {
                g_printerr("Missing --mkv value\n");
                std::exit(1);
            }
            mkvPath_ = resolveMkvPath(argv[++i]);
            if (mkvPath_.empty()) {
                std::exit(1);
            }
        } else if (std::strcmp(argv[i], "--sub-mkv") == 0) {
            if (i + 1 >= argc) {
                g_printerr("Missing --sub-mkv value\n");
                std::exit(1);
            }
            subMkvPath_ = resolveMkvPath(argv[++i]);
            if (subMkvPath_.empty()) {
                std::exit(1);
            }
            secondaryStreamEnabled_ = true;
        } else if (std::strcmp(argv[i], "--codec") == 0) {
            if (i + 1 >= argc) {
                g_printerr("Missing --codec value (expected h264 or h265)\n");
                std::exit(1);
            }
            const char *codec = argv[++i];
            if (std::strcmp(codec, "h264") == 0) {
                videoCodec_ = VideoCodec::H264;
            } else if (std::strcmp(codec, "h265") == 0) {
                videoCodec_ = VideoCodec::H265;
            } else {
                g_printerr("Invalid --codec value: %s (expected h264 or h265)\n", codec);
                std::exit(1);
            }
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            if (!parseInt(argv[++i], 16, 8192, integerValue)) {
                g_printerr("Invalid --width value\n");
                std::exit(1);
            }
            videoWidth_ = integerValue;
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            if (!parseInt(argv[++i], 16, 8192, integerValue)) {
                g_printerr("Invalid --height value\n");
                std::exit(1);
            }
            videoHeight_ = integerValue;
        } else if (std::strcmp(argv[i], "--secondary-width") == 0 && i + 1 < argc) {
            if (!parseInt(argv[++i], 16, 8192, integerValue)) {
                g_printerr("Invalid --secondary-width value\n");
                std::exit(1);
            }
            secondaryWidth_ = integerValue;
        } else if (std::strcmp(argv[i], "--secondary-height") == 0 && i + 1 < argc) {
            if (!parseInt(argv[++i], 16, 8192, integerValue)) {
                g_printerr("Invalid --secondary-height value\n");
                std::exit(1);
            }
            secondaryHeight_ = integerValue;
        } else if (std::strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            if (!parseInt(argv[++i], 1, 240, integerValue)) {
                g_printerr("Invalid --fps value\n");
                std::exit(1);
            }
            videoFps_ = integerValue;
        } else if (std::strcmp(argv[i], "--video-pt") == 0 && i + 1 < argc) {
            if (!parseUnsigned(argv[++i], 0, 127, unsignedValue)) {
                g_printerr("Invalid --video-pt value\n");
                std::exit(1);
            }
            videoPayloadType_ = unsignedValue;
        } else if (std::strcmp(argv[i], "--audio-pt") == 0 && i + 1 < argc) {
            if (!parseUnsigned(argv[++i], 0, 127, unsignedValue)) {
                g_printerr("Invalid --audio-pt value\n");
                std::exit(1);
            }
            audioPayloadType_ = unsignedValue;
        } else if (std::strcmp(argv[i], "--low-latency") == 0) {
            lowLatency_ = true;
        } else if (std::strcmp(argv[i], "--no-low-latency") == 0) {
            lowLatency_ = false;
        } else if (std::strcmp(argv[i], "--tcp-only") == 0) {
            tcpOnly_ = true;
        } else if (std::strcmp(argv[i], "--mpp") == 0) {
            useMpp_ = true;
        } else if (std::strcmp(argv[i], "--sub-resize") == 0) {
            secondaryStreamEnabled_ = true;
        } else if (std::strcmp(argv[i], "--quiet-rtspclient-logs") == 0) {
            quietRtspClientLogs_ = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            g_printerr("Unknown argument: %s\n", argv[i]);
            printUsage(argv[0]);
            std::exit(1);
        }
    }

    if (hasSubMkvSource() && !isMkvSource()) {
        g_printerr("--sub-mkv requires a primary --mkv source\n");
        std::exit(1);
    }
    if (secondaryStreamEnabled_ && isMkvSource() && mountPath_ == secondaryMountPath_) {
        g_printerr("--mount and --secondary-mount must be different\n");
        std::exit(1);
    }
}
