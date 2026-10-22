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

std::string username_by_id(db::Database& database, long long user_id) {
    auto r = database.query_params(
        "SELECT username FROM users WHERE id = ? LIMIT 1;",
        { db::int_cell(user_id) }
    );
    if (r.empty()) return "?";
    return db::row_text(r[0], 0);
}

std::string book_title_by_id(db::Database& database, long long book_id) {
    auto r = database.query_params(
        "SELECT title FROM books WHERE id = ? LIMIT 1;",
        { db::int_cell(book_id) }
    );
    if (r.empty()) return "?";
    return db::row_text(r[0], 0);
}

// --- ping ---

json cmd_ping() {
    json d;
    d["pong"] = true;
    d["ts"]   = static_cast<long long>(std::time(nullptr));
    return rpc_ok(d);
}

// --- register ---

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

json cmd_whoami(const Session& s) {
    json d;
    d["username"] = s.username;
    d["role"]     = s.role;
    d["user_id"]  = s.user_id;
    return rpc_ok(d);
}

// ============================================================
// book.*
// ============================================================

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
    if (args.contains("isbn") && !args["isbn"].is_null())
        isbn = core::normalize_isbn(args["isbn"].get<std::string>());

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

json cmd_book_owner(db::Database& database, const json& args) {
    if (!args.contains("book_id"))
        return rpc_error("bad_request", "book_id required");
    long long book_id = args["book_id"].get<long long>();

    auto loans = core::active_loans_for_book(database, book_id);

    json arr = json::array();
    for (const auto& l : loans) {
        json one;
        one["copy_id"]  = l.copy_id;
        one["user"]     = username_by_id(database, l.user_id);
        one["givedate"] = l.givedate;
        one["enddate"]  = l.enddate ? json(*l.enddate) : json(nullptr);
        arr.push_back(one);
    }
    json d;
    d["book_id"]      = book_id;
    d["active_loans"] = arr;
    return rpc_ok(d);
}

json cmd_book_givedate(db::Database& database, const json& args) {
    if (!args.contains("copy_id"))
        return rpc_error("bad_request", "copy_id required");
    long long copy_id = args["copy_id"].get<long long>();

    auto loan = core::active_loan_for_copy(database, copy_id);
    if (!loan) return rpc_error("not_loaned", "copy is not loaned");

    json d;
    d["copy_id"]  = copy_id;
    d["givedate"] = loan->givedate;
    return rpc_ok(d);
}

json cmd_book_enddate(db::Database& database, const json& args) {
    if (!args.contains("copy_id"))
        return rpc_error("bad_request", "copy_id required");
    long long copy_id = args["copy_id"].get<long long>();

    auto loan = core::active_loan_for_copy(database, copy_id);
    if (!loan) return rpc_error("not_loaned", "copy is not loaned");

    json d;
    d["copy_id"] = copy_id;
    d["enddate"] = loan->enddate ? json(*loan->enddate) : json(nullptr);
    return rpc_ok(d);
}

json cmd_book_request(db::Database& database, const Session& s, const json& args) {
    if (!args.contains("book_id"))
        return rpc_error("bad_request", "book_id required");
    long long book_id = args["book_id"].get<long long>();

    auto b = core::find_by_id(database, book_id);
    if (!b) return rpc_error("not_found", "book not found");

    try {
        long long req_id = core::create_request(database, book_id, s.user_id);
        core::log_info(database, "book.requested", s.user_id,
                       std::string("{\"book_id\":") + std::to_string(book_id) +
                       ",\"request_id\":" + std::to_string(req_id) + "}");
        json d;
        d["request_id"] = req_id;
        d["book_id"]    = book_id;
        d["status"]     = "pending";
        return rpc_ok(d);
    } catch (const std::exception& e) {
        return rpc_error("request_failed", e.what());
    }
}

json cmd_book_wanted(db::Database& database, const Session& s, const json& args) {
    if (!role_at_least(s.role, "moderator"))
        return rpc_error("forbidden", "requires moderator role or higher");

    std::optional<long long> book_id;
    std::optional<long long> user_id;
    if (args.contains("book_id") && !args["book_id"].is_null())
        book_id = args["book_id"].get<long long>();
    if (args.contains("user") && !args["user"].is_null()) {
        std::string uname = args["user"].get<std::string>();
        auto ur = database.query_params(
            "SELECT id FROM users WHERE username = ? LIMIT 1;",
            { db::text_cell(uname) }
        );
        if (!ur.empty()) user_id = db::row_int(ur[0], 0);
    }

    auto reqs = core::list_requests(database, book_id, user_id, "pending");

    json arr = json::array();
    for (const auto& r : reqs) {
        json one;
        one["id"]         = r.id;
        one["book_id"]    = r.book_id;
        one["book"]       = book_title_by_id(database, r.book_id);
        one["user"]       = username_by_id(database, r.user_id);
        one["created_at"] = r.created_at;
        arr.push_back(one);
    }
    json d;
    d["requests"] = arr;
    return rpc_ok(d);
}

// ============================================================
// user.*
// ============================================================

json cmd_user_list(db::Database& database, const Session& s, const json& args) {
    std::string role_filter = args.value("role", "");

    std::vector<db::Row> rows;
    if (role_filter.empty()) {
        rows = database.query(
            "SELECT id, username, role, created_at, last_seen_at, "
            "profile_public, books_public, stats_public "
            "FROM users ORDER BY username;"
        );
    } else {
        rows = database.query_params(
            "SELECT id, username, role, created_at, last_seen_at, "
            "profile_public, books_public, stats_public "
            "FROM users WHERE role = ? ORDER BY username;",
            { db::text_cell(role_filter) }
        );
    }

    if (s.role == "reader") {
        std::vector<db::Row> filtered;
        for (const auto& r : rows) {
            bool pub = db::row_int(r, 5) != 0;
            std::string uname = db::row_text(r, 1);
            if (pub || uname == s.username) filtered.push_back(r);
        }
        rows = std::move(filtered);
    }

    json arr = json::array();
    for (const auto& r : rows) {
        json one;
        one["id"]       = db::row_int(r, 0);
        one["username"] = db::row_text(r, 1);
        one["role"]     = db::row_text(r, 2);
        arr.push_back(one);
    }
    json d;
    d["users"] = arr;
    return rpc_ok(d);
}

json cmd_user_details(db::Database& database, const Session& s, const json& args) {
    if (!args.contains("user"))
        return rpc_error("bad_request", "user required");
    std::string target = args["user"].get<std::string>();

    auto r = database.query_params(
        "SELECT id, username, role, created_at, last_seen_at, "
        "profile_public, books_public, stats_public "
        "FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(target) }
    );
    if (r.empty()) return rpc_error("not_found", "user not found");

    auto& row = r[0];
    bool is_self = (target == s.username);
    bool profile_pub = db::row_int(row, 5) != 0;

    if (s.role == "reader" && !is_self && !profile_pub) {
        return rpc_error("forbidden", "profile is private");
    }

    auto created   = db::row_int(row, 3);
    bool has_ls    = !db::row_is_null(row, 4);
    auto last_seen = has_ls ? db::row_int(row, 4) : 0;

    json d;
    d["id"]             = db::row_int(row, 0);
    d["username"]       = db::row_text(row, 1);
    d["role"]           = db::row_text(row, 2);
    d["created_at"]     = created;
    d["last_seen_at"]   = has_ls ? json(last_seen) : json(nullptr);
    d["profile_public"] = profile_pub;
    d["books_public"]   = db::row_int(row, 6) != 0;
    d["stats_public"]   = db::row_int(row, 7) != 0;
    return rpc_ok(d);
}

json cmd_user_add(db::Database& database, const Session& s, const json& args) {
    if (!role_at_least(s.role, "host"))
        return rpc_error("forbidden", "requires host role");

    if (!args.contains("username") || !args.contains("password"))
        return rpc_error("bad_request", "username, password required");

    std::string username = args["username"].get<std::string>();
    std::string password = args["password"].get<std::string>();
    std::string role = args.value("role", "reader");

    if (role != "reader" && role != "moderator")
        return rpc_error("bad_request", "role must be reader or moderator");

    if (username.empty() || password.empty())
        return rpc_error("bad_request", "username/password empty");

    auto existing = database.query_params(
        "SELECT id FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(username) }
    );
    if (!existing.empty())
        return rpc_error("already_exists", "user already exists");

    auto pw_hash = util::hash_password(password);
    auto now = static_cast<long long>(std::time(nullptr));

    database.exec_params(
        "INSERT INTO users (username, pw_hash, role, created_at) "
        "VALUES (?, ?, ?, ?);",
        {
            db::text_cell(username),
            db::text_cell(pw_hash),
            db::text_cell(role),
            db::int_cell(now)
        }
    );

    long long new_id = database.last_insert_rowid();

    core::log_info(database, "user.added", s.user_id,
                   std::string("{\"username\":\"") + username +
                   "\",\"role\":\"" + role + "\"}");

    json d;
    d["id"]       = new_id;
    d["username"] = username;
    d["role"]     = role;
    return rpc_ok(d);
}

json cmd_user_level(db::Database& database, const Session& s, const json& args) {
    if (!role_at_least(s.role, "host"))
        return rpc_error("forbidden", "requires host role");

    if (!args.contains("user") || !args.contains("role"))
        return rpc_error("bad_request", "user, role required");

    std::string target = args["user"].get<std::string>();
    std::string new_role = args["role"].get<std::string>();

    if (new_role != "reader" && new_role != "moderator")
        return rpc_error("bad_request", "role must be reader or moderator");
    if (target == s.username)
        return rpc_error("self_change", "cannot change your own role");

    auto r = database.query_params(
        "SELECT id, role FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(target) }
    );
    if (r.empty()) return rpc_error("not_found", "user not found");

    std::string current_role = db::row_text(r[0], 1);
    if (current_role == "host")
        return rpc_error("host_change", "cannot change host role");

    database.exec_params(
        "UPDATE users SET role = ? WHERE username = ?;",
        { db::text_cell(new_role), db::text_cell(target) }
    );

    core::log_info(database, "user.level_changed", s.user_id,
                   std::string("{\"username\":\"") + target +
                   "\",\"old\":\"" + current_role +
                   "\",\"new\":\"" + new_role + "\"}");

    json d;
    d["username"] = target;
    d["old_role"] = current_role;
    d["new_role"] = new_role;
    return rpc_ok(d);
}

// ============================================================
// profile.*
// ============================================================

json cmd_profile_show(db::Database& database, const Session& s, const json& args) {
    std::string target = args.value("user", "");
    if (target.empty()) target = s.username;

    auto r = database.query_params(
        "SELECT profile_public, books_public, stats_public "
        "FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(target) }
    );
    if (r.empty()) return rpc_error("not_found", "user not found");

    json d;
    d["username"]       = target;
    d["profile_public"] = db::row_int(r[0], 0) != 0;
    d["books_public"]   = db::row_int(r[0], 1) != 0;
    d["stats_public"]   = db::row_int(r[0], 2) != 0;
    return rpc_ok(d);
}

json cmd_profile_set(db::Database& database, const Session& s, const json& args) {
    if (!args.contains("field") || !args.contains("value"))
        return rpc_error("bad_request", "field, value required");

    std::string field = args["field"].get<std::string>();
    bool value = args["value"].get<bool>();

    std::string column;
    if (field == "profile") column = "profile_public";
    else if (field == "books") column = "books_public";
    else if (field == "stats") column = "stats_public";
    else return rpc_error("bad_request", "unknown field");

    database.exec_params(
        "UPDATE users SET " + column + " = ? WHERE username = ?;",
        { db::int_cell(value ? 1 : 0), db::text_cell(s.username) }
    );

    json d;
    d["username"] = s.username;
    d[field]      = value ? "public" : "private";
    return rpc_ok(d);
}

// ============================================================
// stats
// ============================================================

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

// ============================================================
// logs
// ============================================================

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

// ============================================================
// Главный диспетчер
// ============================================================

json dispatch_rpc(const std::string& library, const json& request) {
    if (!request.contains("cmd") || !request["cmd"].is_string()) {
        return rpc_error("bad_request", "cmd field is required");
    }
    std::string cmd = request["cmd"].get<std::string>();
    json args = request.value("args", json::object());

    auto database = core::open_library_db(library);

    // без авторизации
    if (cmd == "ping")     return cmd_ping();
    if (cmd == "register") return cmd_register(database, library, args);
    if (cmd == "login")    return cmd_login(database, args);

    // всё остальное — по токену
    auto session = auth(database, request);
    if (!session) return rpc_error("unauthorized", "invalid or missing token");

    // book
    if (cmd == "book.list")     return cmd_book_list(database, args);
    if (cmd == "book.info")     return cmd_book_info(database, args);
    if (cmd == "book.add")      return cmd_book_add(database, *session, args);
    if (cmd == "book.grant")    return cmd_book_grant(database, *session, args);
    if (cmd == "book.return")   return cmd_book_return(database, *session, args);
    if (cmd == "book.owner")    return cmd_book_owner(database, args);
    if (cmd == "book.givedate") return cmd_book_givedate(database, args);
    if (cmd == "book.enddate")  return cmd_book_enddate(database, args);
    if (cmd == "book.request")  return cmd_book_request(database, *session, args);
    if (cmd == "book.wanted")   return cmd_book_wanted(database, *session, args);

    // user
    if (cmd == "user.list")    return cmd_user_list(database, *session, args);
    if (cmd == "user.details") return cmd_user_details(database, *session, args);
    if (cmd == "user.add")     return cmd_user_add(database, *session, args);
    if (cmd == "user.level")   return cmd_user_level(database, *session, args);

    // profile
    if (cmd == "profile.show") return cmd_profile_show(database, *session, args);
    if (cmd == "profile.set")  return cmd_profile_set(database, *session, args);

    // stats, logs
    if (cmd == "stats")        return cmd_stats(database, *session, args);
    if (cmd == "logs")         return cmd_logs(database, *session, args);

    // whoami
    if (cmd == "whoami")       return cmd_whoami(*session);

    return rpc_error("unknown_cmd", "unknown command: " + cmd);
}

} // namespace libcore::net
