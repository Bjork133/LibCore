#include "net/http_client.hpp"

#include <httplib.h>

#include <stdexcept>

namespace libcore::net {

namespace {
// httplib требует http:// префикс или отдельный хост/порт
}

HttpClient::HttpClient(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

json HttpClient::call(const json& request) {
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(10, 0);

    auto res = cli.Post("/rpc", request.dump(), "application/json");
    if (!res) {
        throw std::runtime_error("cannot connect to " + host_ + ":" + std::to_string(port_));
    }
    if (res->status != 200) {
        throw std::runtime_error("HTTP " + std::to_string(res->status));
    }
    try {
        return json::parse(res->body);
    } catch (...) {
        throw std::runtime_error("invalid JSON from server");
    }
}

json HttpClient::call(const std::string& cmd,
                      const json& args,
                      const std::string& token) {
    json req;
    req["cmd"] = cmd;
    if (!args.empty()) req["args"] = args;
    if (!token.empty()) req["token"] = token;
    return call(req);
}

} // namespace libcore::net
