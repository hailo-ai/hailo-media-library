#pragma once

#include <optional>
#include <string>
#include <memory>

namespace files_utils
{

typedef std::shared_ptr<int> SharedFd;
SharedFd make_shared_fd(int fd);

std::optional<int> read_int_from_file(const std::string &path);
std::optional<std::string> read_string_from_file(const std::string &path);
} // namespace files_utils
