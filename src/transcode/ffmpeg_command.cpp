#include "transcode/ffmpeg_command.hpp"

#include <filesystem>
#include <string>

namespace sonarium::transcode {

std::string_view extension_for(TargetCodec codec) noexcept {
    switch (codec) {
        case TargetCodec::mp3:
            return "mp3";
        case TargetCodec::aac_lc:
            return "m4a";
    }
    return "mp3";
}

std::string output_path_for(std::string_view input_path, TargetCodec codec) {
    std::filesystem::path const p{std::string{input_path}};
    auto stem = p;
    stem.replace_extension();
    auto out = stem;
    out += '.';
    out += extension_for(codec);
    return out.string();
}

namespace {

[[nodiscard]] std::string_view ffmpeg_codec_for(TargetCodec codec) noexcept {
    switch (codec) {
        case TargetCodec::mp3:
            return "libmp3lame";
        case TargetCodec::aac_lc:
            return "aac";
    }
    return "libmp3lame";
}

} // namespace

std::vector<std::string> build_ffmpeg_argv(TranscodeRequest const& req) {
    std::vector<std::string> argv;
    argv.reserve(16);
    argv.emplace_back("ffmpeg");
    argv.emplace_back("-hide_banner");
    argv.emplace_back("-nostdin");
    argv.emplace_back("-loglevel");
    argv.emplace_back("error");
    if (req.overwrite) {
        argv.emplace_back("-y");
    } else {
        argv.emplace_back("-n");
    }
    argv.emplace_back("-i");
    argv.emplace_back(req.input_path);
    argv.emplace_back("-vn"); // strip any embedded artwork stream
    argv.emplace_back("-codec:a");
    argv.emplace_back(std::string{ffmpeg_codec_for(req.codec)});
    argv.emplace_back("-b:a");
    argv.emplace_back(std::to_string(req.bitrate_kbps) + "k");
    argv.emplace_back(req.output_path);
    return argv;
}

} // namespace sonarium::transcode
