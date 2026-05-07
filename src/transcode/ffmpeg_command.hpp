#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "transcode/transcode_request.hpp"

namespace sonarium::transcode {

// Filename extension (no leading dot) for the encoder's container.
[[nodiscard]] std::string_view extension_for(TargetCodec codec) noexcept;

// Replace the extension on `input_path` with the one matching `codec`. The
// output path is the input's stem suffixed with `.<ext>`. If the input has no
// extension at all, `.<ext>` is appended.
[[nodiscard]] std::string output_path_for(std::string_view input_path, TargetCodec codec);

// Build the argv vector to hand to execvp / posix_spawn. Argv[0] is "ffmpeg"
// (lookup happens via PATH); the rest is the encoder configuration.
[[nodiscard]] std::vector<std::string> build_ffmpeg_argv(TranscodeRequest const& req);

} // namespace sonarium::transcode
