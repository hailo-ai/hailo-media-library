/**
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 **/
#pragma once

#include <iostream>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/schema.h"

namespace common
{
/**
 * @brief validate that the json file (that its data is in stream) complies with the scehma rules.
 *
 * @param stream rapidjson::FileReadStream byte stream holding the json config file data
 * @param json_schema const char * holding the json schema rules
 * @return true in case the config file complies with the scehma rules.
 * @return false in case the config file doesn't comply with the scehma rules.
 */
bool validate_json_with_schema(rapidjson::FileReadStream stream, const char *json_schema);

} // namespace common
