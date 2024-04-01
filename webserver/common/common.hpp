#pragma once
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <httplib.h>

#define V4L2_DEVICE_NAME "/dev/video0"
#ifndef MEDIALIB_LOCAL_SERVER
#define UDP_HOST "10.0.0.2"
#else
#define UDP_HOST "127.0.0.1"
#endif

template <typename T>
inline bool json_extract_value(const nlohmann::json &json, const std::string &key, T &out, std::string *return_msg = nullptr)
{
    if (json.find(key) == json.end())
    {
        if (return_msg)
        {
            *return_msg = "Missing " + key + " in JSON";
        }
        return T();
    }

    return json[key].get<T>();
}

template <typename T>
inline bool http_request_extract_value(const httplib::Request &req, const std::string &key, T &out, std::string *return_msg = nullptr)
{
    auto j_body = nlohmann::json::parse(req.body);

    return json_extract_value<T>(j_body, key, out, return_msg);
}