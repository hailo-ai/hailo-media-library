#include <iostream>
#include "arguments_parser.hpp"
#include "gyro_device.hpp"

namespace arguments_parser
{
    int handle_arguments(int argc, char **argv,
                         std::string &iio_device_name,
                         std::string &output_path,
                         std::string &device_freq,
                         std::string &gyro_scale)
    {
        CLI::App app("gyro calibration tool");

        iio_device_name = DEFAULT_GYRO_DEVICE_NAME;
        app.add_option("-n,--iio-device-name", iio_device_name, "IIO device name")->capture_default_str();

        output_path = DEFAULT_GYRO_OUTPUT_PATH;
        app.add_option("-o,--output-path", output_path, "IIO device calibration output path")->capture_default_str();

        device_freq = DEFAULT_DEVICE_ODR;
        app.add_option("-f,--device-freq", device_freq, "IIO device freq, optional value: 208.000000")->capture_default_str();

        gyro_scale = DEFAULT_GYRO_SCALE;
        app.add_option("-s,--gyro-scale", gyro_scale, "IIO gyro scale")->capture_default_str();

        try
        {
            app.parse(argc, argv);
        }
        catch (const CLI::ParseError &e)
        {
            app.exit(e);
            return -1;
        }

        return 0;
    }
} // namespace arguments_parser