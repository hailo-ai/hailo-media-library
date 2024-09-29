#include "httplib_utils.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>

class HTTPServer::Impl
{
private:
    httplib::Server m_server;

public:
    Impl();
    static std::shared_ptr<HTTPServer::Impl> create();
    void listen(const std::string &host, int port);
    void set_mount_point(const std::string &mount_point, const std::string &path);
    void Get(const std::string &pattern, std::function<void()> callback);
    void Get(const std::string &pattern, std::function<nlohmann::json()> callback);
    void Put(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback);
    void Patch(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback);
    void Post(const std::string &pattern, std::function<void(const nlohmann::json &)> callback);
    void Post(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback);
    void Redirect(const std::string &pattern, const std::string &target);
    void Delete(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback);
};

HTTPServer::HTTPServer()
{
    m_impl = HTTPServer::Impl::create();
}

std::shared_ptr<HTTPServer> HTTPServer::create()
{
    return std::make_shared<HTTPServer>();
}

void HTTPServer::listen(const std::string &host, int port)
{
    m_impl->listen(host, port);
}

void HTTPServer::set_mount_point(const std::string &mount_point, const std::string &path)
{
    m_impl->set_mount_point(mount_point, path);
}

void HTTPServer::Get(const std::string &pattern, std::function<void()> callback)
{
    m_impl->Get(pattern, callback);
}

void HTTPServer::Get(const std::string &pattern, std::function<nlohmann::json()> callback)
{
    m_impl->Get(pattern, callback);
}

void HTTPServer::Put(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    m_impl->Put(pattern, callback);
}

void HTTPServer::Patch(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    m_impl->Patch(pattern, callback);
}

void HTTPServer::Post(const std::string &pattern, std::function<void(const nlohmann::json &)> callback)
{
    m_impl->Post(pattern, callback);
}

void HTTPServer::Post(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    m_impl->Post(pattern, callback);
}

void HTTPServer::Redirect(const std::string &pattern, const std::string &target)
{
    m_impl->Redirect(pattern, target);
}

void HTTPServer::Delete(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    m_impl->Delete(pattern, callback);
}

HTTPServer::Impl::Impl() : m_server()
{
}

std::shared_ptr<HTTPServer::Impl> HTTPServer::Impl::create()
{
    return std::make_shared<HTTPServer::Impl>();
}

void HTTPServer::Impl::listen(const std::string &host, int port)
{
    m_server.listen(host.c_str(), port);
}

void HTTPServer::Impl::set_mount_point(const std::string &mount_point, const std::string &path)
{
    m_server.set_mount_point(mount_point.c_str(), path.c_str());
}

void HTTPServer::Impl::Get(const std::string &pattern, std::function<void()> callback)
{
    m_server.Get(pattern.c_str(), [callback](const httplib::Request &req, httplib::Response &res)
                 { callback(); });
}

void HTTPServer::Impl::Get(const std::string &pattern, std::function<nlohmann::json()> callback)
{
    m_server.Get(pattern.c_str(), [callback](const httplib::Request &req, httplib::Response &res)
                 { res.set_content(callback().dump(), "application/json"); });
}

void HTTPServer::Impl::Put(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    m_server.Put(pattern.c_str(), [callback](const httplib::Request &req, httplib::Response &res)
                 {
                     nlohmann::json json = nlohmann::json::parse(req.body);
                     nlohmann::json response = callback(json);
                     res.set_content(response.dump(), "application/json"); });
}

void HTTPServer::Impl::Patch(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    m_server.Patch(pattern.c_str(), [callback](const httplib::Request &req, httplib::Response &res)
                   {
                       nlohmann::json json = nlohmann::json::parse(req.body);
                       nlohmann::json response = callback(json);
                       res.set_content(response.dump(), "application/json"); });
}

void HTTPServer::Impl::Post(const std::string &pattern, std::function<void(const nlohmann::json &)> callback)
{
    m_server.Post(pattern.c_str(), [callback](const httplib::Request &req, httplib::Response &res)
                  {
                      nlohmann::json json = nlohmann::json::parse(req.body);
                      callback(json); });
}

void HTTPServer::Impl::Post(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    m_server.Post(pattern.c_str(), [callback](const httplib::Request &req, httplib::Response &res)
                  {
                      nlohmann::json json = nlohmann::json::parse(req.body);
                      nlohmann::json response = callback(json);
                      res.set_content(response.dump(), "application/json"); });
}

void HTTPServer::Impl::Redirect(const std::string &pattern, const std::string &target)
{
    m_server.Get(pattern.c_str(), [target](const httplib::Request &req, httplib::Response &res)
                 { res.set_redirect(target.c_str()); });
}

void HTTPServer::Impl::Delete(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    m_server.Delete(pattern.c_str(), [callback](const httplib::Request &req, httplib::Response &res)
                    {
                        nlohmann::json json = nlohmann::json::parse(req.body);
                        nlohmann::json response = callback(json);
                        res.set_content(response.dump(), "application/json"); });
}