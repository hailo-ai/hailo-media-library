#pragma once
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <httplib.h>

#define V4L2_DEVICE_NAME "/dev/video0"
#define TRIPLE_A_CONFIG_PATH "/usr/bin/3aconfig.json"
#define SONY_CONFIG_PATH "/usr/bin/sony_imx678.xml"

#define ISP_DEFUALT_FILEPATH(x) (std::string("/home/root/isp_configs/default/") + x)
#define ISP_DENOISE_FILEPATH(x) (std::string("/home/root/isp_configs/denoise/") + x)
#define ISP_BACKLIGHT_FILEPATH(x) (std::string("/home/root/isp_configs/backlight/") + x)

#define ISP_DEFAULT_3A_CONFIG ISP_DEFUALT_FILEPATH("3aconfig.json")
#define ISP_DENOISE_3A_CONFIG ISP_DENOISE_FILEPATH("3aconfig.json")
#define ISP_BACKLIGHT_COMPENSATION_3A_CONFIG ISP_BACKLIGHT_FILEPATH("3aconfig.json")

#define ISP_DEFAULT_SONY_CONFIG ISP_DEFUALT_FILEPATH("sony_imx678.xml")
#define ISP_DENOISE_SONY_CONFIG ISP_DENOISE_FILEPATH("sony_imx678.xml")

#ifndef MEDIALIB_LOCAL_SERVER
#define UDP_HOST "10.0.0.2"
#else
#define UDP_HOST "127.0.0.1"
#endif

inline void override_file(const std::string &src, const std::string &dst)
{
    std::cout << "Overriding file " << src << " to " << dst << std::endl;

#ifndef MEDIALIB_LOCAL_SERVER
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
#endif
}

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