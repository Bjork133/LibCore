#include "handlers/remote.hpp"

#include "net/http_client.hpp"

#include <cstdio>
#include <stdexcept>

namespace libcore::handlers {

namespace {

// Простой pretty-print JSON в человекочитаемом виде.
// Не идеальный, но читаемый.
void pretty_print(const json& j, int indent = 0) {
    std::string pad(indent * 2, ' ');

    if (j.is_object()) {
        if (j.empty()) { std::printf("{}\n"); return; }
        for (auto it = j.begin(); it != j.end(); ++it) {
            std::printf("%s%s: ", pad.c_str(), it.key().c_str());
            if (it.value().is_object() || it.value().is_array()) {
                std::printf("\n");
                pretty_print(it.value(), indent + 1);
            } else {
                std::printf("%s\n", it.value().dump().c_str());
            }
        }
    } else if (j.is_array()) {
        for (const auto& item : j) {
            std::printf("%s- ", pad.c_str());
            if (item.is_object() || item.is_array()) {
                std::printf("\n");
                pretty_print(item, indent + 1);
            } else {
                std::printf("%s\n", item.dump().c_str());
            }
        }
    } else {
        std::printf("%s%s\n", pad.c_str(), j.dump().c_str());
    }
}

} // namespace

json remote_raw(const core::Profile& profile,
                const std::string& cmd,
                const json& args) {
    if (!profile.host || !profile.port) {
        throw std::runtime_error("remote profile missing host/port");
    }
    net::HttpClient cli(*profile.host, *profile.port);

    std::string token = profile.session_token.value_or("");
    return cli.call(cmd, args, token);
}

int remote_call(const core::Profile& profile,
                const std::string& cmd,
                const json& args,
                bool json_out) {
    try {
        auto resp = remote_raw(profile, cmd, args);

        if (!resp.value("ok", false)) {
            // ошибка
            std::string code    = resp["error"].value("code", "unknown");
            std::string message = resp["error"].value("message", "");
            if (json_out) {
                std::fprintf(stderr, "%s\n", resp.dump().c_str());
            } else {
                std::fprintf(stderr, "libcore: %s (%s)\n",
                             message.c_str(), code.c_str());
            }
            return 1;
        }

        // успех
        if (json_out) {
            std::printf("%s\n", resp.dump().c_str());
        } else {
            pretty_print(resp["data"]);
        }
        return 0;
    } catch (const std::exception& e) {
        if (json_out) {
            json j;
            j["ok"] = false;
            j["error"]["code"] = "network";
            j["error"]["message"] = e.what();
            std::fprintf(stderr, "%s\n", j.dump().c_str());
        } else {
            std::fprintf(stderr, "libcore: %s\n", e.what());
        }
        return 1;
    }
}

} // namespace libcore::handlers
