
#include <string>
#include <stdexcept>
#include <iostream>
#include "media_library/cloexec_fstream.hpp"
#include <filesystem>
#include <fstream> // IWYU pragma: keep
#include <sstream> // IWYU pragma: keep
#include <exception>

std::string read_string_from_file(const std::string &file_path)
{
    cloexec::ifstream file(file_path);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open file: " + file_path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
} // Ensure this brace properly closes the read_string_from_file function

void safe_remove_symlink_target(const std::filesystem::path &symlink)
{
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(symlink)))
    {
        try
        {
            std::filesystem::path target = std::filesystem::read_symlink(symlink);
            if (std::filesystem::exists(target))
            {
                std::filesystem::remove(target); // Remove the target only if it exists
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error reading symlink " << symlink << ": " << e.what() << '\n';
        }
        std::filesystem::remove(symlink); // Remove the symlink itself
    }
}
