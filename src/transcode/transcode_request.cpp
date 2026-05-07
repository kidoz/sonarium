#include "transcode/transcode_request.hpp"

namespace sonarium::transcode {

sonarium::media::AudioCodec to_audio_codec(TargetCodec c) noexcept {
    switch (c) {
        case TargetCodec::mp3:
            return sonarium::media::AudioCodec::mp3;
        case TargetCodec::aac_lc:
            return sonarium::media::AudioCodec::aac_lc;
    }
    return sonarium::media::AudioCodec::mp3;
}

sonarium::media::AudioContainer to_audio_container(TargetCodec c) noexcept {
    switch (c) {
        case TargetCodec::mp3:
            return sonarium::media::AudioContainer::mp3;
        case TargetCodec::aac_lc:
            return sonarium::media::AudioContainer::mp4;
    }
    return sonarium::media::AudioContainer::mp3;
}

} // namespace sonarium::transcode
