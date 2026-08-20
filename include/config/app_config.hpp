#pragma once

#include <string>

enum class StreamMode {
    Both,
    Video,
    Audio
};

enum class VideoCodec {
    H264,
    H265
};

class AppConfig final {
public:
    void parse(int argc, char *argv[]);
    void applyMkvMediaInfo(bool hasVideo, bool hasAudio, VideoCodec codec);
    void applySubMkvMediaInfo(bool hasVideo, bool hasAudio, VideoCodec codec);

    bool isMkvSource() const;
    bool hasVideo() const;
    bool hasAudio() const;
    bool hasSubMkvSource() const;
    bool subMkvHasVideo() const;
    bool subMkvHasAudio() const;
    bool secondaryStreamAvailable() const;

    StreamMode streamMode() const;
    VideoCodec videoCodec() const;
    const char *videoCodecName() const;
    const char *videoEncoderFactoryName() const;
    const char *videoEncoderRawFormat() const;
    VideoCodec subMkvVideoCodec() const;
    const char *subMkvVideoCodecName() const;

    const std::string &host() const;
    const std::string &mountPath() const;
    const std::string &secondaryMountPath() const;
    const std::string &videoDevice() const;
    const std::string &audioDevice() const;
    const std::string &mkvPath() const;
    const std::string &subMkvPath() const;
    unsigned int port() const;
    int videoWidth() const;
    int videoHeight() const;
    int videoFps() const;
    int secondaryWidth() const;
    int secondaryHeight() const;
    unsigned int videoPayloadType() const;
    unsigned int audioPayloadType() const;
    bool lowLatency() const;
    bool quietRtspClientLogs() const;
    bool tcpOnly() const;
    bool useMpp() const;
    bool secondaryStreamEnabled() const;

private:
    static bool parseInt(const char *value, int minValue, int maxValue, int &result);
    static bool parseUnsigned(const char *value, unsigned int minValue,
                              unsigned int maxValue, unsigned int &result);
    static std::string resolveMkvPath(const char *value);
    static void printUsage(const char *programName);

    StreamMode streamMode_ = StreamMode::Both;
    VideoCodec videoCodec_ = VideoCodec::H264;
    std::string host_ = "0.0.0.0";
    std::string mountPath_ = "/stream";
    std::string secondaryMountPath_ = "/stream-low";
    std::string videoDevice_ = "/dev/video0";
    std::string audioDevice_ = "plughw:CARD=rockchipes8388,DEV=0";
    std::string mkvPath_;
    std::string subMkvPath_;
    VideoCodec subMkvVideoCodec_ = VideoCodec::H264;
    unsigned int port_ = 8554;
    int videoWidth_ = 1280;
    int videoHeight_ = 800;
    int videoFps_ = 30;
    int secondaryWidth_ = 640;
    int secondaryHeight_ = 360;
    unsigned int videoPayloadType_ = 96;
    unsigned int audioPayloadType_ = 8;
    bool lowLatency_ = true;
    bool quietRtspClientLogs_ = false;
    bool tcpOnly_ = false;
    bool useMpp_ = false;
    bool secondaryStreamEnabled_ = false;
    bool subMkvHasVideo_ = false;
    bool subMkvHasAudio_ = false;
};
