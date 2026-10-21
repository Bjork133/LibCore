#include "net/rpc.hpp"

#include "core/books.hpp"
#include "core/library_db.hpp"
#include "core/loans.hpp"
#include "core/logs.hpp"
#include "core/profiles.hpp"
#include "core/requests.hpp"
#include "core/server_meta.hpp"
#include "core/sessions.hpp"
#include "core/settings.hpp"
#include "db/database.hpp"
#include "util/crypto.hpp"
#include "util/dateparse.hpp"

#include <ctime>
#include <optional>

namespace libcore::net {

json rpc_ok(json data) {
    json j;
    j["ok"] = true;
    j["data"] = std::move(data);
    return j;
}

json rpc_error(const std::string& code, const std::string& message) {
    json j;
    j["ok"] = false;
    j["error"]["code"] = code;
    j["error"]["message"] = message;
    return j;
}

namespace {

// --- контекст сессии ---

struct Session {
    long long user_id = 0;
    std::string username;
    std::string role;
};

std::optional<Session> auth(db::Database& database, const json& req) {
    if (!req.contains("token") || !req["token"].is_string()) return std::nullopt;
    std::string token = req["token"].get<std::string>();

    auto s = core::get_session(database, token);
    if (!s) return std::nullopt;

    // user_id по username
    auto r = database.query_params(
        "SELECT id FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(s->username) }
    );
    if (r.empty()) return std::nullopt;

    Session out;
    out.user_id  = db::row_int(r[0], 0);
    out.username = s->username;
    out.role     = s->role;
    return out;
}

bool role_at_least(const std::string& role, const std::string& required) {
    auto rank = [](const std::string& r) -> int {
        if (r == "host") return 3;
        if (r == "moderator") return 2;
        if (r == "reader") return 1;
        return 0;
    };
    return rank(role) >= rank(required);
}

// --- ping ---

json cmd_ping() {
    json d;
    d["pong"] = true;
    d["ts"]   = static_cast<long long>(std::time(nullptr));
    return rpc_ok(d);
}

// --- register (по коду) ---

json cmd_register(db::Database& database, const std::string& library,
                  const json& args) {
    if (!args.contains("code") || !args.contains("username") || !args.contains("password")) {
        return rpc_error("bad_request", "code, username, password required");
    }

    auto meta = core::read_meta(library);
    if (!meta) return rpc_error("no_meta", "server.json missing");

    std::string code = args["code"].get<std::string>();
    if (code != meta->code) {
        return rpc_error("bad_code", "invalid library code");
    }

    std::string username = args["username"].get<std::string>();
    std::string password = args["password"].get<std::string>();

    if (username.empty() || password.empty()) {
        return rpc_error("bad_request", "username/password empty");
    }

    // Существует?
    auto existing = database.query_params(
        "SELECT id FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(username) }
    );
    if (!existing.empty()) {
        return rpc_error("already_exists", "user already exists");
    }

    auto pw_hash = util::hash_password(password);
    auto now = static_cast<long long>(std::time(nullptr));

    database.exec_params(
        "INSERT INTO users (username, pw_hash, role, created_at) "
        "VALUES (?, ?, 'reader', ?);",
        {
            db::text_cell(username),
            db::text_cell(pw_hash),
            db::int_cell(now)
        }
    );

    long long user_id = database.last_insert_rowid();

    core::log_info(database, "user.registered", user_id,
                   std::string("{\"username\":\"") + username + "\"}");

    // Сразу логиним
    auto token = core::create_session(database, user_id, 30LL * 24 * 3600);

    json d;
    d["username"] = username;
    d["role"]     = "reader";
    d["token"]    = token;
    return rpc_ok(d);
}

// --- login ---

json cmd_login(db::Database& database, const json& args) {
    if (!args.contains("username") || !args.contains("password")) {
        return rpc_error("bad_request", "username, password required");
    }
    std::string username = args["username"].get<std::string>();
    std::string password = args["password"].get<std::string>();

    auto r = database.query_params(
        "SELECT id, pw_hash, role FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(username) }
    );
    if (r.empty()) {
        return rpc_error("user_not_found", "user not found");
    }

    long long user_id = db::row_int(r[0], 0);
    std::string pw_hash = db::row_text(r[0], 1);
    std::string role    = db::row_text(r[0], 2);

    if (!util::verify_password(password, pw_hash)) {
        core::log_warn(database, "auth.failed", user_id,
                       std::string("{\"username\":\"") + username +
                       "\",\"reason\":\"invalid_password\"}");
        return rpc_error("invalid_password", "invalid password");
    }

    core::delete_user_sessions(database, user_id);
    auto token = core::create_session(database, user_id, 30LL * 24 * 3600);

    database.exec_params(
        "UPDATE users SET last_seen_at = ? WHERE id = ?;",
        { db::int_cell(static_cast<long long>(std::time(nullptr))),
          db::int_cell(user_id) }
    );

    core::log_info(database, "user.login", user_id,
                   std::string("{\"username\":\"") + username + "\"}");

    json d;
    d["username"] = username;
    d["role"]     = role;
    d["token"]    = token;
    return rpc_ok(d);
}

// --- whoami ---

json cmd_whoami(db::Database& database, const Session& s) {
    json d;
    d["username"] = s.username;
    d["role"]     = s.role;
    d["user_id"]  = s.user_id;
    return rpc_ok(d);
}

// --- book.list ---

json cmd_book_list(db::Database& database, const json& args) {
    bool available_only = args.value("available", false);
    auto books = core::list_books(database);

    json arr = json::array();
    for (const auto& b : books) {
        int av = core::count_available(database, b.id);
        if (available_only && av == 0) continue;
        json one;
        one["id"]        = b.id;
        one["title"]     = b.title;
        one["author"]    = b.author;
        one["year"]      = b.year ? json(*b.year) : json(nullptr);
        one["isbn"]      = b.isbn ? json(*b.isbn) : json(nullptr);
        one["available"] = av;
        arr.push_back(one);
    }
    json d;
    d["books"] = arr;
    return rpc_ok(d);
}

// --- book.info ---

json cmd_book_info(db::Database& database, const json& args) {
    if (args.contains("id")) {
        long long id = args["id"].get<long long>();
        auto b = core::find_by_id(database, id);
        if (!b) return rpc_error("not_found", "book not found");

        auto copies = core::list_copies(database, id);
        int avail = core::count_available(database, id);

        json d;
        d["id"]        = b->id;
        d["title"]     = b->title;
        d["author"]    = b->author;
        d["year"]      = b->year ? json(*b->year) : json(nullptr);
        d["isbn"]      = b->isbn ? json(*b->isbn) : json(nullptr);
        d["available"] = avail;
        json cj = json::array();
        for (const auto& c : copies) {
            json one;
            one["id"]     = c.id;
            one["status"] = c.status;
            cj.push_back(one);
        }
        d["copies"] = cj;
        return rpc_ok(d);
    }

    std::string query = args.value("query", "");
    auto books = core::search_books(database, query);
    json arr = json::array();
    for (const auto& b : books) {
        json one;
        one["id"]     = b.id;
        one["title"]  = b.title;
        one["author"] = b.author;
        one["year"]   = b.year ? json(*b.year) : json(nullptr);
        one["isbn"]   = b.isbn ? json(*b.isbn) : json(nullptr);
        arr.push_back(one);
    }
    json d;
    d["books"] = arr;
    return rpc_ok(d);
}

// --- book.add ---

json cmd_book_add(db::Database& database, const Session& s, const json& args) {
    if (!role_at_least(s.role, "moderator"))
        return rpc_error("forbidden", "requires moderator role or higher");

    if (!args.contains("title") || !args.contains("author"))
        return rpc_error("bad_request", "title, author required");

    std::string title  = args["title"].get<std::string>();
    std::string author = args["author"].get<std::string>();

    std::optional<int> year;
    if (args.contains("year") && !args["year"].is_null())
        year = args["year"].get<int>();

    std::optional<std::string> isbn;
    if (args.contains("isbn") && !args["isbn"].is_null()) {
        isbn = core::normalize_isbn(args["isbn"].get<std::string>());
    }

    int count = args.value("count", 1);
    if (count < 1) count = 1;

    auto res = core::add_book(database, title, author, year, isbn, s.user_id);
    auto copies = core::add_copies(database, res.book_id, count);

    core::log_info(database, "book.added", s.user_id,
                   std::string("{\"book_id\":") + std::to_string(res.book_id) +
                   ",\"title\":\"" + title + "\"}");

    json d;
    d["book_id"]  = res.book_id;
    d["created"]  = res.created;
    d["copies"]   = copies.size();
    d["copy_ids"] = copies;
    return rpc_ok(d);
}

// --- book.grant ---

json cmd_book_grant(db::Database& database, const Session& s, const json& args) {
    if (!role_at_least(s.role, "moderator"))
        return rpc_error("forbidden", "requires moderator role or higher");

    if (!args.contains("book_id") || !args.contains("to"))
        return rpc_error("bad_request", "book_id, to required");

    long long book_id = args["book_id"].get<long long>();
    std::string to = args["to"].get<std::string>();

    auto ur = database.query_params(
        "SELECT id FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(to) }
    );
    if (ur.empty()) return rpc_error("user_not_found", "user not found");
    long long user_id = db::row_int(ur[0], 0);

    std::optional<long long> until_ts;
    if (args.contains("until") && !args["until"].is_null()) {
        std::string until = args["until"].get<std::string>();
        auto cfg = core::load_settings();
        auto ts = util::parse_date(until, cfg.time_format);
        if (!ts) return rpc_error("bad_date", "invalid until date");
        until_ts = *ts;
    }

    try {
        long long loan_id = core::grant_loan(database, book_id, user_id, until_ts);

        core::log_info(database, "book.granted", s.user_id,
                       std::string("{\"book_id\":") + std::to_string(book_id) +
                       ",\"to\":\"" + to + "\"}");

        json d;
        d["loan_id"] = loan_id;
        d["book_id"] = book_id;
        d["to"]      = to;
        return rpc_ok(d);
    } catch (const std::exception& e) {
        return rpc_error("grant_failed", e.what());
    }
}

// --- book.return ---

json cmd_book_return(db::Database& database, const Session& s, const json& args) {
    if (!role_at_least(s.role, "moderator"))
        return rpc_error("forbidden", "requires moderator role or higher");

    if (!args.contains("copy_id"))
        return rpc_error("bad_request", "copy_id required");

    long long copy_id = args["copy_id"].get<long long>();
    bool ok = core::return_loan(database, copy_id);
    if (!ok) return rpc_error("not_loaned", "copy is not loaned");

    core::log_info(database, "book.returned", s.user_id,
                   std::string("{\"copy_id\":") + std::to_string(copy_id) + "}");

    json d;
    d["copy_id"] = copy_id;
    return rpc_ok(d);
}

// --- stats ---

json cmd_stats(db::Database& database, const Session& s, const json& args) {
    std::string target = args.value("user", "");
    if (target.empty()) target = s.username;

    auto ur = database.query_params(
        "SELECT id, created_at, last_seen_at, stats_public FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(target) }
    );
    if (ur.empty()) return rpc_error("not_found", "user not found");

    long long user_id      = db::row_int(ur[0], 0);
    long long created_at   = db::row_int(ur[0], 1);
    bool has_ls            = !db::row_is_null(ur[0], 2);
    long long last_seen_at = has_ls ? db::row_int(ur[0], 2) : 0;
    bool stats_public      = db::row_int(ur[0], 3) != 0;

    bool is_self = (target == s.username);
    if (!is_self && s.role == "reader" && !stats_public) {
        return rpc_error("forbidden", "stats are private");
    }

    auto now = static_cast<long long>(std::time(nullptr));
    auto q = [&](const char* sql, std::vector<db::Cell> params) -> long long {
        auto r = database.query_params(sql, params);
        if (r.empty() || r[0].empty()) return 0;
        return db::row_int(r[0], 0);
    };

    json d;
    d["username"]         = target;
    d["created_at"]       = created_at;
    d["last_seen_at"]     = has_ls ? json(last_seen_at) : json(nullptr);
    d["total_loans"]      = q("SELECT COUNT(*) FROM loans WHERE user_id = ?;", { db::int_cell(user_id) });
    d["active_loans"]     = q("SELECT COUNT(*) FROM loans WHERE user_id = ? AND returndate IS NULL;", { db::int_cell(user_id) });
    d["returned"]         = q("SELECT COUNT(*) FROM loans WHERE user_id = ? AND returndate IS NOT NULL;", { db::int_cell(user_id) });
    d["overdue"]          = q("SELECT COUNT(*) FROM loans WHERE user_id = ? AND returndate IS NULL AND enddate IS NOT NULL AND enddate < ?;", { db::int_cell(user_id), db::int_cell(now) });
    d["requests_pending"] = q("SELECT COUNT(*) FROM requests WHERE user_id = ? AND status = 'pending';", { db::int_cell(user_id) });
    return rpc_ok(d);
}

// --- logs ---

json cmd_logs(db::Database& database, const Session& s, const json& args) {
    if (!role_at_least(s.role, "moderator"))
        return rpc_error("forbidden", "requires moderator role or higher");

    std::optional<std::string> level;
    std::optional<std::string> event;
    if (args.contains("level") && !args["level"].is_null())
        level = args["level"].get<std::string>();
    if (args.contains("event") && !args["event"].is_null())
        event = args["event"].get<std::string>();

    int tail = args.value("tail", 50);

    auto entries = core::list_logs(database, level, event, std::nullopt, tail);

    json arr = json::array();
    for (const auto& l : entries) {
        json one;
        one["id"]      = l.id;
        one["ts"]      = l.ts;
        one["level"]   = l.level;
        one["event"]   = l.event;
        one["actor"]   = l.actor_id ? json(*l.actor_id) : json(nullptr);
        one["payload"] = l.payload ? json(*l.payload) : json(nullptr);
        arr.push_back(one);
    }
    json d;
    d["logs"] = arr;
    return rpc_ok(d);
}

} // namespace

// --- Главный диспетчер ---

json dispatch_rpc(const std::string& library, const json& request) {
    if (!request.contains("cmd") || !request["cmd"].is_string()) {
        return rpc_error("bad_request", "cmd field is required");
    }
    std::string cmd = request["cmd"].get<std::string>();
    json args = request.value("args", json::object());

    auto database = core::open_library_db(library);

    // Команды без авторизации
    if (cmd == "ping")     return cmd_ping();
    if (cmd == "register") return cmd_register(database, library, args);
    if (cmd == "login")    return cmd_login(database, args);

    // Всё остальное — по токену
    auto session = auth(database, request);
    if (!session) return rpc_error("unauthorized", "invalid or missing token");

    if (cmd == "whoami")      return cmd_whoami(database, *session);
    if (cmd == "book.list")   return cmd_book_list(database, args);
    if (cmd == "book.info")   return cmd_book_info(database, args);
    if (cmd == "book.add")    return cmd_book_add(database, *session, args);
    if (cmd == "book.grant")  return cmd_book_grant(database, *session, args);
    if (cmd == "book.return") return cmd_book_return(database, *session, args);
    if (cmd == "stats")       return cmd_stats(database, *session, args);
    if (cmd == "logs")        return cmd_logs(database, *session, args);

    return rpc_error("unknown_cmd", "unknown command: " + cmd);
}

} // namespace libcore::net
