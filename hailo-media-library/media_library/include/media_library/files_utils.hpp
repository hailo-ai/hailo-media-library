#pragma once

#include <optional>
#include <string>
#include <memory>
#include "media_library_types.hpp"

namespace files_utils
{

typedef std::shared_ptr<int> SharedFd;
SharedFd make_shared_fd(int fd);

std::optional<int> read_int_from_file(const std::string &path);
std::optional<std::string> read_string_from_file(const std::string &path);
media_library_return write_string_to_file_atomic(const std::string &path, const std::string &content);

} // namespace files_utils
