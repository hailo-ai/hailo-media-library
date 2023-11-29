/*
 * Copyright (c) 2017-2023 Hailo Technologies Ltd. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once

#include <iostream>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"

namespace common
{
    /**
     * @brief validate that the json file (that its data is in stream) complies with
     * the scehma rules.
     *
     * @param stream rapidjson::FileReadStream byte stream holding the json config
     * file data
     * @param json_schema const char * holding the json schema rules
     * @return true in case the config file complies with the scehma rules.
     * @return false in case the config file doesn't comply with the scehma rules.
     */
    bool validate_json_with_schema(rapidjson::FileReadStream stream,
                                   const char *json_schema)
    {
        rapidjson::Document d;
        d.Parse(json_schema);
        rapidjson::SchemaDocument sd(d);
        rapidjson::SchemaValidator validator(sd);
        rapidjson::Reader reader;
        if (!reader.Parse(stream, validator) &&
            reader.GetParseErrorCode() != rapidjson::kParseErrorTermination)
        {
            // Schema validator error would cause kParseErrorTermination, which will
            // handle it in next step.
            std::cerr << "JSON error (offset "
                      << static_cast<unsigned>(reader.GetErrorOffset())
                      << "): " << GetParseError_En(reader.GetParseErrorCode())
                      << std::endl;
            throw std::runtime_error("Input is not a valid JSON");
        }

        // Check the validation result
        if (validator.IsValid())
        {
            return true;
        }
        else
        {
            std::cerr << "Input JSON is invalid" << std::endl;
            rapidjson::StringBuffer sb;
            validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
            std::cerr << "Invalid schema: " << sb.GetString() << std::endl;
            std::cerr << "Invalid keyword: " << validator.GetInvalidSchemaKeyword()
                      << std::endl;
            sb.Clear();
            validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
            std::cerr << "Invalid document: " << sb.GetString() << std::endl;
            throw std::runtime_error(
                "json config file doesn't follow schema rules");
        }
        return false;
    }

    bool validate_json_with_schema_stringstream(rapidjson::StringStream stream,
                                                const char *json_schema)
    {
        rapidjson::Document d;
        d.Parse(json_schema);
        rapidjson::SchemaDocument sd(d);
        rapidjson::SchemaValidator validator(sd);
        rapidjson::Reader reader;
        if (!reader.Parse(stream, validator) &&
            reader.GetParseErrorCode() != rapidjson::kParseErrorTermination)
        {
            // Schema validator error would cause kParseErrorTermination, which will
            // handle it in next step.
            std::cerr << "JSON error (offset "
                      << static_cast<unsigned>(reader.GetErrorOffset())
                      << "): " << GetParseError_En(reader.GetParseErrorCode())
                      << std::endl;
            throw std::runtime_error("Input is not a valid JSON");
        }

        // Check the validation result
        if (validator.IsValid())
        {
            return true;
        }
        else
        {
            std::cerr << "Input JSON is invalid" << std::endl;
            rapidjson::StringBuffer sb;
            validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
            std::cerr << "Invalid schema: " << sb.GetString() << std::endl;
            std::cerr << "Invalid keyword: " << validator.GetInvalidSchemaKeyword()
                      << std::endl;
            sb.Clear();
            validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
            std::cerr << "Invalid document: " << sb.GetString() << std::endl;
            throw std::runtime_error(
                "json config file doesn't follow schema rules");
        }
        return false;
    }

} // namespace common