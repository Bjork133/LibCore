#include "net/http_server.hpp"

#include "net/rpc.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace libcore::net {

using json = nlohmann::json;

struct HttpServerImpl {
    httplib::Server srv;
};

HttpServer::HttpServer(std::string library, int port)
    : library_(std::move(library)), port_(port)
{
    impl_ = new HttpServerImpl();
}

HttpServer::~HttpServer() {
    if (impl_) {
        auto* p = static_cast<HttpServerImpl*>(impl_);
        p->srv.stop();
        delete p;
        impl_ = nullptr;
    }
}

void HttpServer::start() {
    auto* p = static_cast<HttpServerImpl*>(impl_);
    auto* srv = &p->srv;

    srv->Post("/rpc", [this](const httplib::Request& req, httplib::Response& res) {
        json request;
        try {
            request = json::parse(req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(rpc_error("bad_json", "invalid JSON").dump(),
                            "application/json");
            return;
        }

        json response;
        try {
            response = dispatch_rpc(library_, request);
        } catch (const std::exception& e) {
            response = rpc_error("internal", e.what());
        }

        res.set_content(response.dump(), "application/json");
    });

    // GET / — простая проверка живости
    srv->Get("/", [this](const httplib::Request&, httplib::Response& res) {
        json j;
        j["library"] = library_;
        j["ok"]      = true;
        res.set_content(j.dump(), "application/json");
    });

    std::fprintf(stderr, "libcore: HTTP server listening on 0.0.0.0:%d (library: %s)\n",
                 port_, library_.c_str());

    if (!srv->listen("0.0.0.0", port_)) {
        throw std::runtime_error("cannot bind to port " + std::to_string(port_));
    }
}

void HttpServer::stop() {
    auto* p = static_cast<HttpServerImpl*>(impl_);
    if (p) p->srv.stop();
}

} // namespace libcore::net
