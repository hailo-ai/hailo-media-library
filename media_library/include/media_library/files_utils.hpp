#pragma once

#include <optional>
#include <string>

namespace files_utils
{

std::optional<int> read_int_from_file(const std::string &path);
std::optional<std::string> read_string_from_file(const std::string &path);
} // namespace files_utils
