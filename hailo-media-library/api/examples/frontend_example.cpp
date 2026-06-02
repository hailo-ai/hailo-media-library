/**
 * DEPRECATED: frontend_example
 * * This monolithic example has been split into specific use-cases to improve
 * readability and maintainability.
 * * Please refer to the new example files for specific functionality:
 * * * 1. Basic Recording & Setup:
 * --> basic_h264_recording_to_file.cpp
 * * * 2. OSD, Custom Overlays, and Privacy Masks:
 * --> osd_privacy_example.cpp
 * * * 3. Profile Switching (HDR/Night Mode) & Callbacks:
 * --> profile_switching_example.cpp
 * * * 4. Runtime Controls (Bitrate/Quality tuning):
 * --> dynamic_controls_example.cpp
 * * * 5. Rotation (0, 90, 180, 270 degrees):
 * --> rotation_example.cpp
 * * * 6. Resolution Change (Runtime reconfiguration):
 * --> resolution_change_example.cpp
 * * * This file is kept as a placeholder to prevent build breakage but will be removed in future versions.
 */

#include <iostream>

int main(int /*argc*/, char ** /*argv*/)
{
    std::cout << "\n=============================================================" << std::endl;
    std::cout << " [WARNING] This example is deprecated." << std::endl;
    std::cout << "=============================================================" << std::endl;
    std::cout << "The functionality has been moved to separate example files." << std::endl;
    std::cout << "Please use the specific executable for your use case:" << std::endl;
    std::cout << "\n 1. Basic Recording:" << std::endl;
    std::cout << "    - basic_h264_recording_to_file" << std::endl;

    std::cout << "\n 2. OSD & Privacy:" << std::endl;
    std::cout << "    - osd_privacy_example" << std::endl;

    std::cout << "\n 3. Profile Switching:" << std::endl;
    std::cout << "    - profile_switching_example" << std::endl;

    std::cout << "\n 4. Dynamic Controls:" << std::endl;
    std::cout << "    - dynamic_controls_example" << std::endl;

    std::cout << "\n 5. Rotation:" << std::endl;
    std::cout << "    - rotation_example" << std::endl;

    std::cout << "\n 6. Resolution Change:" << std::endl;
    std::cout << "    - resolution_change_example" << std::endl;

    std::cout << "\n(Note: JPEG variants are also available for relevant examples)" << std::endl;
    std::cout << "=============================================================\n" << std::endl;

    return 0;
}
