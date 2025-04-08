#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <filesystem>

// Function declarations
std::string read_string_from_file(const std::string &file_path);
void safe_remove_symlink_target(const std::filesystem::path &symlink);
