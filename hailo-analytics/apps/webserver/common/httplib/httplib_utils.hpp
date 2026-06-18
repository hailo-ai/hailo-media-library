#pragma once

#include <nlohmann/json.hpp>
#include <stddef.h>
#include <memory>
#include <functional>
#include <exception>
#include <string>
#include <utility>

// Forward declarations — avoid pulling in <httplib.h> (~141K preprocessor lines)
namespace httplib
{
struct Request;
struct Response;
class DataSink;
} // namespace httplib

class HTTPServer
{
  private:
    class Impl;

    std::shared_ptr<Impl> m_impl;

  public:
    using ExceptionHandler =
        std::function<void(const httplib::Request &req, httplib::Response &res, std::exception_ptr ep)>;

    HTTPServer();
    ~HTTPServer();
    static std::unique_ptr<HTTPServer> create();
    void listen(const std::string &host, int port);
    void set_mount_point(const std::string &mount_point, const std::string &path);
    void Get(const std::string &pattern, std::function<void()> callback);
    void Get(const std::string &pattern, std::function<nlohmann::json()> callback);
    void Put(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback);
    void Patch(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback);
    void Post(const std::string &pattern, std::function<void(const nlohmann::json &)> callback);
    void Post(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback);
    void Post(const std::string &pattern, std::function<bool(const std::string &, const std::string &)> callback);
    void Post(const std::string &pattern,
              std::function<std::pair<nlohmann::json, int>(const nlohmann::json &)> callback);
    void Post_Content_Provider(
        const std::string &pattern,
        std::function<
            std::pair<size_t, std::function<size_t(size_t, size_t, httplib::DataSink &)>>(const nlohmann::json &)>
            callback);
    void Get(const std::string &pattern, std::function<void(const httplib::Request &, httplib::Response &)> callback);
    void Redirect(const std::string &pattern, const std::string &target);
    void Delete(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback);
    void set_exception_handler(const ExceptionHandler &exception_handler);

    void Unregister(const std::string &pattern);
};
