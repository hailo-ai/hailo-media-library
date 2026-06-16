#include <signal.h>
#include <stdlib.h>
#include <thread>
#include <memory>
#include <string>

#include "media_library_logger.hpp"
#include "gyro_device.hpp"
#include "arguments_parser.hpp"

#define MODULE_NAME LoggerType::Eis

extern std::unique_ptr<GyroDevice> gyroApi;

static void handle_sig(int)
{
    if (gyroApi->stopRunning())
        LOGGER__MODULE__INFO(MODULE_NAME, "Notify process to finish...");
}

static void set_handler(int signal_nb, void (*handler)(int))
{
    struct sigaction sig;
    sigaction(signal_nb, NULL, &sig);
    sig.sa_handler = handler;
    sigaction(signal_nb, &sig, NULL);
}

int main(int argc, char *argv[])
{
    int argument_handling_results;
    sigset_t set, oldset;
    std::string output_path, iio_device_name, device_freq, gyro_scale;

    argument_handling_results =
        arguments_parser::handle_arguments(argc, argv, iio_device_name, output_path, device_freq, gyro_scale);
    if (argument_handling_results == -1)
    {
        return 0;
    }

    gyroApi = std::make_unique<GyroDevice>(iio_device_name, device_freq, std::stod(gyro_scale));
    gyro_status_t status = gyroApi->configure();
    if (status != GYRO_STATUS_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to configure GyroDev, status: {}", status);
        return EXIT_FAILURE;
    }
    set_handler(SIGINT, &handle_sig);
    set_handler(SIGTERM, &handle_sig);
    sigfillset(&set);
    pthread_sigmask(SIG_BLOCK, &set, &oldset);
    std::thread gyroThread = std::thread(&GyroDevice::dump_rec_samples, gyroApi.get(), output_path);
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);

    status = gyroApi->run();
    if (status != GYRO_STATUS_SUCCESS)
    {
        LOGGER__MODULE__ERROR(MODULE_NAME, "Failed to run GyroDev, status: {}", status);
        return EXIT_FAILURE;
    }
    gyroThread.join();
    return EXIT_SUCCESS;
}
