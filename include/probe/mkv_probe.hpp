#pragma once

#include "config/app_config.hpp"

#include <string>

class MkvMediaInfo final {
public:
    MkvMediaInfo();
    MkvMediaInfo(bool hasVideo, bool hasAudio, VideoCodec videoCodec);

    bool hasVideo() const;
    bool hasAudio() const;
    VideoCodec videoCodec() const;

private:
    bool hasVideo_;
    bool hasAudio_;
    VideoCodec videoCodec_;
};

class MkvProbe final {
public:
    bool inspect(const std::string &path, MkvMediaInfo &mediaInfo) const;
};
