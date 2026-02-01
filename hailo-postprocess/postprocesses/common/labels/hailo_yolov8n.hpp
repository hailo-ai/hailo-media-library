/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once
#include <map>
namespace common
{
static std::map<uint8_t, std::string> hailo_yolov8n = {
    {1, "person"}, {2, "vehicle"}, {3, "face"}, {4, "license_plate"}};
}
