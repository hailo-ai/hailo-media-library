#include <CLI/CLI.hpp>

namespace arguments_parser
{
int handle_arguments(int argc, char **argv, std::string &iio_device_name, std::string &output_path,
                     std::string &device_freq, std::string &gyro_scale);
}
