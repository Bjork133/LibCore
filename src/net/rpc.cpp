#include "net/rpc.hpp"

#include "api/auth.hpp"
#include "api/book.hpp"
#include "api/common.hpp"
#include "api/library.hpp"
#include "api/logs.hpp"
#include "api/profile.hpp"
#include "api/stats.hpp"
#include "api/user.hpp"

#include <optional>

namespace libcore::net {

using json = nlohmann::json;

json dispatch_rpc(const std::string& library, const json& request) {
    if (!request.contains("cmd") || !request["cmd"].is_string()) {
        return api::err("bad_request", "cmd field is required");
    }
    std::string cmd = request["cmd"].get<std::string>();
    json args = request.value("args", json::object());
    std::string token = request.value("token", "");

    // --- без авторизации ---
    if (cmd == "ping") {
        json d;
        d["pong"] = true;
        d["ts"]   = static_cast<long long>(std::time(nullptr));
        return api::ok(d);
    }
    if (cmd == "register") {
        return api::register_user(library,
            args.value("code", ""),
            args.value("username", ""),
            args.value("password", ""));
    }
    if (cmd == "login") {
        return api::login(library,
            args.value("username", ""),
            args.value("password", ""));
    }

    // --- с авторизацией ---
    if (cmd == "whoami") {
        return api::whoami(library, token);
    }

    // book
    if (cmd == "book.list") {
        return api::book_list(library, token, args.value("available", false));
    }
    if (cmd == "book.info") {
        if (args.contains("id") && !args["id"].is_null()) {
            return api::book_info_by_id(library, args["id"].get<long long>());
        }
        return api::book_info_by_query(library, args.value("query", ""));
    }
    if (cmd == "book.add") {
        std::optional<int> year;
        if (args.contains("year") && !args["year"].is_null()) year = args["year"].get<int>();
        std::optional<std::string> isbn;
        if (args.contains("isbn") && !args["isbn"].is_null()) isbn = args["isbn"].get<std::string>();
        return api::book_add(library, token,
            args.value("title", ""),
            args.value("author", ""),
            year, isbn,
            args.value("count", 1));
    }
    if (cmd == "book.grant") {
        if (!args.contains("book_id") || !args.contains("to"))
            return api::err("bad_request", "book_id, to required");
        std::optional<std::string> until;
        if (args.contains("until") && !args["until"].is_null()) until = args["until"].get<std::string>();
        return api::book_grant(library, token,
            args["book_id"].get<long long>(),
            args["to"].get<std::string>(),
            until);
    }
    if (cmd == "book.return") {
        if (!args.contains("copy_id"))
            return api::err("bad_request", "copy_id required");
        return api::book_return(library, token, args["copy_id"].get<long long>());
    }
    if (cmd == "book.owner") {
        if (!args.contains("book_id"))
            return api::err("bad_request", "book_id required");
        return api::book_owner(library, args["book_id"].get<long long>());
    }
    if (cmd == "book.givedate") {
        if (!args.contains("copy_id"))
            return api::err("bad_request", "copy_id required");
        return api::book_givedate(library, args["copy_id"].get<long long>());
    }
    if (cmd == "book.enddate") {
        if (!args.contains("copy_id"))
            return api::err("bad_request", "copy_id required");
        return api::book_enddate(library, args["copy_id"].get<long long>());
    }
    if (cmd == "book.request") {
        if (!args.contains("book_id"))
            return api::err("bad_request", "book_id required");
        return api::book_request(library, token, args["book_id"].get<long long>());
    }
    if (cmd == "book.wanted") {
        std::optional<long long> book_id;
        std::optional<std::string> user;
        if (args.contains("book_id") && !args["book_id"].is_null())
            book_id = args["book_id"].get<long long>();
        if (args.contains("user") && !args["user"].is_null())
            user = args["user"].get<std::string>();
        return api::book_wanted(library, token, book_id, user);
    }

    // user
    if (cmd == "user.list") {
        std::optional<std::string> role;
        if (args.contains("role") && !args["role"].is_null())
            role = args["role"].get<std::string>();
        return api::user_list(library, token, role);
    }
    if (cmd == "user.details") {
        if (!args.contains("user"))
            return api::err("bad_request", "user required");
        return api::user_details(library, token, args["user"].get<std::string>());
    }
    if (cmd == "user.add") {
        if (!args.contains("username") || !args.contains("password"))
            return api::err("bad_request", "username, password required");
        return api::user_add(library, token,
            args["username"].get<std::string>(),
            args["password"].get<std::string>(),
            args.value("role", "reader"));
    }
    if (cmd == "user.level") {
        if (!args.contains("user") || !args.contains("role"))
            return api::err("bad_request", "user, role required");
        return api::user_level(library, token,
            args["user"].get<std::string>(),
            args["role"].get<std::string>());
    }

    // profile
    if (cmd == "profile.show") {
        std::optional<std::string> user;
        if (args.contains("user") && !args["user"].is_null())
            user = args["user"].get<std::string>();
        return api::profile_show(library, token, user);
    }
    if (cmd == "profile.set") {
        if (!args.contains("field") || !args.contains("value"))
            return api::err("bad_request", "field, value required");
        return api::profile_set(library, token,
            args["field"].get<std::string>(),
            args["value"].get<bool>());
    }

    // stats, logs
    if (cmd == "stats") {
        std::optional<std::string> user;
        if (args.contains("user") && !args["user"].is_null())
            user = args["user"].get<std::string>();
        return api::stats(library, token, user);
    }
    if (cmd == "logs") {
        std::optional<std::string> level;
        std::optional<std::string> event;
        std::optional<std::string> since;
        if (args.contains("level") && !args["level"].is_null())
            level = args["level"].get<std::string>();
        if (args.contains("event") && !args["event"].is_null())
            event = args["event"].get<std::string>();
        if (args.contains("since") && !args["since"].is_null())
            since = args["since"].get<std::string>();
        return api::logs(library, token, level, event, since, args.value("tail", 50));
    }

    return api::err("unknown_cmd", "unknown command: " + cmd);
}

} // namespace libcore::net
