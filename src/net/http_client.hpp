#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace libcore::net {

using json = nlohmann::json;

class HttpClient {
public:
    HttpClient(std::string host, int port);

    // Отправить RPC-запрос. Возвращает JSON-ответ.
    // Бросает std::runtime_error при сетевой ошибке.
    json call(const json& request);

    // Удобный враппер: cmd + args + optional token.
    json call(const std::string& cmd,
              const json& args = json::object(),
              const std::string& token = "");

private:
    std::string host_;
    int port_;
};

} // namespace libcore::net
