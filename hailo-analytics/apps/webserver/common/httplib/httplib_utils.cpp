#include "httplib_utils.hpp"
#include "common/logger_macros.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <ctime>
#include <httplib.h>
#include <unordered_map>
#include <mutex>

enum HTTPMethod
{
    METHOD_GET,
    METHOD_PUT,
    METHOD_PATCH,
    METHOD_POST,
    METHOD_DELETE,
    METHOD_REDIRECT
};

HTTPMethod string_to_http_method(const std::string &method_str)
{
    if (method_str == "GET")
        return METHOD_GET;
    else if (method_str == "PUT")
        return METHOD_PUT;
    else if (method_str == "PATCH")
        return METHOD_PATCH;
    else if (method_str == "POST")
        return METHOD_POST;
    else if (method_str == "DELETE")
        return METHOD_DELETE;
    else if (method_str == "REDIRECT")
        return METHOD_REDIRECT;
    else
        throw std::invalid_argument("Invalid HTTP method string: " + method_str);
}

typedef std::function<void(const httplib::Request &, httplib::Response &)> RouteHandlerFunc;

class HTTPServer::Impl
{
  private:
    httplib::Server m_server;
    std::mutex m_routes_mutex;

    std::map<HTTPMethod, std::map<std::string, RouteHandlerFunc>> m_routes;
    bool m_routing_initialized = false;

    void initialize_routing_handler();
    void register_route(HTTPMethod method, const std::string &pattern, RouteHandlerFunc handler);
    void unregister_route(const std::string &pattern);
    void handle_request(HTTPMethod method, const httplib::Request &req, httplib::Response &res);

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
    void set_cors();
    void set_exception_handler(const ExceptionHandler &exception_handler);
    void Unregister(const std::string &pattern);
};

HTTPServer::HTTPServer()
{
    m_impl = HTTPServer::Impl::create();
    m_impl->set_cors();
}

HTTPServer::~HTTPServer()
{
    WEBSERVER_LOG_INFO("HTTPServer destroyed");
}

std::unique_ptr<HTTPServer> HTTPServer::create()
{
    return std::make_unique<HTTPServer>();
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

void HTTPServer::Post(const std::string &pattern,
                      std::function<bool(const std::string &, const std::string &)> callback)
{
    m_impl->Post(pattern, callback);
}

void HTTPServer::Post(const std::string &pattern,
                      std::function<std::pair<nlohmann::json, int>(const nlohmann::json &)> callback)
{
    m_impl->Post(pattern, callback);
}

void HTTPServer::Post_Content_Provider(
    const std::string &pattern,
    std::function<std::pair<size_t, std::function<size_t(size_t, size_t, httplib::DataSink &)>>(const nlohmann::json &)>
        callback)
{
    m_impl->Post_Content_Provider(pattern, callback);
}

void HTTPServer::Get(const std::string &pattern,
                     std::function<void(const httplib::Request &, httplib::Response &)> callback)
{
    m_impl->Get(pattern, callback);
}

void HTTPServer::Redirect(const std::string &pattern, const std::string &target)
{
    m_impl->Redirect(pattern, target);
}

void HTTPServer::Delete(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    m_impl->Delete(pattern, callback);
}

void HTTPServer::set_exception_handler(const ExceptionHandler &exception_handler)
{
    m_impl->set_exception_handler(exception_handler);
}

void HTTPServer::Unregister(const std::string &pattern)
{
    m_impl->Unregister(pattern);
}

HTTPServer::Impl::Impl() : m_server()
{
    initialize_routing_handler();
}

std::shared_ptr<HTTPServer::Impl> HTTPServer::Impl::create()
{
    return std::make_shared<HTTPServer::Impl>();
}

void HTTPServer::Impl::initialize_routing_handler()
{
    if (m_routing_initialized)
        return;

    m_routing_initialized = true;
    m_server.Get(".*", [&](const httplib::Request &req, httplib::Response &res) {
        handle_request(HTTPMethod::METHOD_GET, req, res);
    });
    m_server.Post(".*", [&](const httplib::Request &req, httplib::Response &res) {
        handle_request(HTTPMethod::METHOD_POST, req, res);
    });
    m_server.Delete(".*", [&](const httplib::Request &req, httplib::Response &res) {
        handle_request(HTTPMethod::METHOD_DELETE, req, res);
    });
    m_server.Put(".*", [&](const httplib::Request &req, httplib::Response &res) {
        handle_request(HTTPMethod::METHOD_PUT, req, res);
    });
    m_server.Patch(".*", [&](const httplib::Request &req, httplib::Response &res) {
        handle_request(HTTPMethod::METHOD_PATCH, req, res);
    });
}

void HTTPServer::Impl::register_route(HTTPMethod method, const std::string &pattern,
                                      std::function<void(const httplib::Request &, httplib::Response &)> handler)
{
    std::lock_guard<std::mutex> lock(m_routes_mutex);
    m_routes[method][pattern] = handler;
}

void HTTPServer::Impl::unregister_route(const std::string &pattern)
{
    std::lock_guard<std::mutex> lock(m_routes_mutex);
    for (auto &method_routes : m_routes)
    {
        if (method_routes.second.find(pattern) == method_routes.second.end())
            continue;
        method_routes.second.erase(pattern);
    }
}

void HTTPServer::Impl::handle_request(HTTPMethod /*method*/, const httplib::Request &req, httplib::Response &res)
{
    RouteHandlerFunc handler;
    // use scope to unlock mutex before calling handler
    {
        std::lock_guard<std::mutex> lock(m_routes_mutex);
        // Check if we have handlers for this path
        auto method_it = m_routes.find(string_to_http_method(req.method));
        if (method_it == m_routes.end())
        {
            res.status = httplib::StatusCode::NotFound_404;
            return;
        }

        // Find the handler for this method
        auto handler_it = method_it->second.find(req.path);
        if (handler_it == method_it->second.end())
        {
            res.status = httplib::StatusCode::NotFound_404;
            return;
        }
        handler = handler_it->second;
    }
    // Call the handler
    handler(req, res);
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
    register_route(HTTPMethod::METHOD_GET, pattern,
                   [callback](const httplib::Request & /*req*/, httplib::Response & /*res*/) { callback(); });
}

void HTTPServer::Impl::Get(const std::string &pattern, std::function<nlohmann::json()> callback)
{
    register_route(HTTPMethod::METHOD_GET, pattern,
                   [callback](const httplib::Request & /*req*/, httplib::Response &res) {
                       res.set_content(callback().dump(), "application/json");
                   });
}

void HTTPServer::Impl::Get(const std::string &pattern,
                           std::function<void(const httplib::Request &, httplib::Response &)> callback)
{
    register_route(HTTPMethod::METHOD_GET, pattern, callback);
}

void HTTPServer::Impl::Put(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    register_route(HTTPMethod::METHOD_PUT, pattern, [callback](const httplib::Request &req, httplib::Response &res) {
        nlohmann::json json = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        nlohmann::json response = callback(json);
        res.set_content(response.dump(), "application/json");
    });
}

void HTTPServer::Impl::Patch(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    register_route(HTTPMethod::METHOD_PATCH, pattern, [callback](const httplib::Request &req, httplib::Response &res) {
        nlohmann::json json = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        nlohmann::json response = callback(json);
        res.set_content(response.dump(), "application/json");
    });
}

void HTTPServer::Impl::Post(const std::string &pattern, std::function<void(const nlohmann::json &)> callback)
{
    register_route(
        HTTPMethod::METHOD_POST, pattern, [callback](const httplib::Request &req, httplib::Response & /*res*/) {
            nlohmann::json json = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
            callback(json);
        });
}

void HTTPServer::Impl::Post(const std::string &pattern, std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    register_route(
        HTTPMethod::METHOD_POST, pattern, [callback, pattern](const httplib::Request &req, httplib::Response &res) {
            nlohmann::json json = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
            nlohmann::json response = callback(json);
            res.set_content(response.dump(), "application/json");
        });
}

void HTTPServer::Impl::Post(const std::string &pattern,
                            std::function<std::pair<nlohmann::json, int>(const nlohmann::json &)> callback)
{
    register_route(HTTPMethod::METHOD_POST, pattern, [callback](const httplib::Request &req, httplib::Response &res) {
        nlohmann::json json = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        std::pair<nlohmann::json, int> response = callback(json);
        res.status = response.second;
        res.set_content(response.first.dump(), "application/json");
    });
}

void HTTPServer::Impl::Post(const std::string &pattern,
                            std::function<bool(const std::string &, const std::string &)> callback)
{
    register_route(HTTPMethod::METHOD_POST, pattern, [callback](const httplib::Request &req, httplib::Response &res) {
        if (!req.has_file("file"))
        {
            res.status = 400;
            res.set_content("No file provided", "text/plain");
            return;
        }
        const auto &file = req.get_file_value("file");
        if (callback(file.filename, file.content))
        {
            res.status = 200;
            res.set_content("File uploaded", "text/plain");
        }
        else
        {
            res.status = 500;
            res.set_content("File upload failed", "text/plain");
        }
    });
}

void HTTPServer::Impl::Post_Content_Provider(
    const std::string &pattern,
    std::function<std::pair<size_t, std::function<size_t(size_t, size_t, httplib::DataSink &)>>(const nlohmann::json &)>
        callback)
{
    register_route(HTTPMethod::METHOD_POST, pattern, [callback](const httplib::Request &req, httplib::Response &res) {
        nlohmann::json json = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        auto [total_size, content_provider] = callback(json);
        res.set_content_provider(total_size, "application/octet-stream", content_provider);
    });
}

void HTTPServer::Impl::Redirect(const std::string &pattern, const std::string &target)
{
    register_route(HTTPMethod::METHOD_GET, pattern, [target](const httplib::Request & /*req*/, httplib::Response &res) {
        res.set_redirect(target.c_str());
    });
}

void HTTPServer::Impl::Delete(const std::string &pattern,
                              std::function<nlohmann::json(const nlohmann::json &)> callback)
{
    register_route(HTTPMethod::METHOD_DELETE, pattern, [callback](const httplib::Request &req, httplib::Response &res) {
        nlohmann::json json = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        nlohmann::json response = callback(json);
        res.set_content(response.dump(), "application/json");
    });
}

void HTTPServer::Impl::set_cors()
{
    m_server.set_pre_routing_handler([](const httplib::Request &req, httplib::Response &res) {
        // Add CORS headers to all responses
        res.set_header("Access-Control-Allow-Origin", "*"); // Allow all origins
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.set_header("Access-Control-Allow-Credentials", "true");

        // Handle preflight OPTIONS requests
        if (req.method == "OPTIONS")
        {
            res.status = 204; // No Content
            return httplib::Server::HandlerResponse::Handled;
        }

        std::time_t now = std::time(nullptr);
        res.set_header("X-Timestamp", std::to_string(now));

        return httplib::Server::HandlerResponse::Unhandled; // Allow other requests to proceed
    });
}

void HTTPServer::Impl::set_exception_handler(const ExceptionHandler &exception_handler)
{
    m_server.set_exception_handler(exception_handler);
}

void HTTPServer::Impl::Unregister(const std::string &pattern)
{
    unregister_route(pattern);
}
