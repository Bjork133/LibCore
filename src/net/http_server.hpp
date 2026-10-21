#pragma once

#include <string>

namespace libcore::net {

class HttpServer {
public:
    HttpServer(std::string library, int port);
    ~HttpServer();

    // Блокируется до вызова stop() или сигнала.
    // Бросает std::runtime_error, если порт занят.
    void start();

    // Останавливает сервер. Можно из другого потока.
    void stop();

private:
    std::string library_;
    int port_;
    void* impl_ = nullptr;   // pimpl, чтобы не тащить httplib.h в заголовок
};

} // namespace libcore::net
