#pragma once

#include <expected>
#include <string>

#include "transcode/transcode_request.hpp"

namespace sonarium::transcode {

// Spawn `ffmpeg` (looked up via PATH), wait for it to finish, and capture the
// last few KiB of stderr. Returns an error string when the process can't be
// spawned at all (binary missing, fork failed); a non-zero exit code is
// reported via TranscodeResult::exit_code so callers can choose how to react.
[[nodiscard]] std::expected<TranscodeResult, std::string> run_ffmpeg(TranscodeRequest const& req);

} // namespace sonarium::transcode
