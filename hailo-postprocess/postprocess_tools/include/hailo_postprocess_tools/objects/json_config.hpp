/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once

#include "rapidjson/filereadstream.h"
#include <string>

namespace common
{
/**
 * @brief validate that the json content complies with the schema rules.
 *
 * @param json_content std::string holding the full JSON document contents
 * @param json_schema const char * holding the json schema rules
 * @return true in case the config file complies with the scehma rules.
 * @return false in case the config file doesn't comply with the scehma rules.
 */
bool validate_json_with_schema(const std::string &json_content, const char *json_schema);

} // namespace common
