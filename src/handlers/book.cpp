#include "handlers/book.hpp"

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
#include "handlers/remote.hpp"
#include "util/dateparse.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <ctime>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace libcore::handlers {

using json = nlohmann::json;

namespace {

// --- контекст ---

struct Ctx {
    std::string profile_name;
    std::string library;
    std::string username;
    std::string role;
    long long   user_id = 0;
    std::optional<std::string> session_token;
};

std::optional<Ctx> load_ctx(const std::optional<std::string>& explicit_profile) {
    std::string profile_name = explicit_profile.value_or("");
    auto all = core::load_profiles();
    if (profile_name.empty()) profile_name = all.default_name;
    if (profile_name.empty()) return std::nullopt;

    auto prof = core::get_profile(profile_name);
    if (!prof) return std::nullopt;

    Ctx c;
    c.profile_name  = prof->name;
    c.library       = prof->library;
    c.username      = prof->username;
    c.session_token = prof->session_token;
    return c;
}

bool resolve_session(Ctx& c, db::Database& database) {
    if (!c.session_token) return false;
    auto s = core::get_session(database, *c.session_token);
    if (!s) return false;
    c.role = s->role;
    auto r = database.query_params(
        "SELECT id FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(c.username) }
    );
    if (r.empty()) return false;
    c.user_id = db::row_int(r[0], 0);
    return true;
}

// --- парсинг ---

struct BookOpts {
    std::vector<std::string> positional;
    std::optional<std::string> profile;
    std::optional<long long>   id;
    std::optional<std::string> title;
    std::optional<std::string> author;
    std::optional<int>         year;
    std::optional<std::string> isbn;
    std::optional<int>         count;
    std::optional<std::string> to;
    std::optional<std::string> until;
    std::optional<long long>   request_id;
    std::optional<std::string> user_filter;
    bool available = false;
};

BookOpts parse_opts(const std::vector<std::string>& args) {
    BookOpts o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];

        auto take = [&](std::string_view flag) -> std::optional<std::string> {
            if (a.size() > flag.size() && a.starts_with(flag) && a[flag.size()] == '=') {
                return a.substr(flag.size() + 1);
            }
            if (a == flag) {
                if (i + 1 >= args.size())
                    throw std::runtime_error(std::string("flag ") + std::string(flag) + " requires a value");
                return args[++i];
            }
            return std::nullopt;
        };

        if (auto v = take("--profile")) { o.profile = *v; continue; }
        if (auto v = take("--id")) {
            try { o.id = std::stoll(*v); }
            catch (...) { throw std::runtime_error("invalid --id: " + *v); }
            continue;
        }
        if (auto v = take("--title"))  { o.title  = *v; continue; }
        if (auto v = take("--author")) { o.author = *v; continue; }
        if (auto v = take("--year")) {
            try { o.year = std::stoi(*v); }
            catch (...) { throw std::runtime_error("invalid --year: " + *v); }
            continue;
        }
        if (auto v = take("--isbn")) { o.isbn = *v; continue; }
        if (auto v = take("--count")) {
            try { o.count = std::stoi(*v); }
            catch (...) { throw std::runtime_error("invalid --count: " + *v); }
            continue;
        }
        if (auto v = take("--to"))   { o.to   = *v; continue; }
        if (auto v = take("--until")) { o.until = *v; continue; }
        if (auto v = take("--request")) {
            try { o.request_id = std::stoll(*v); }
            catch (...) { throw std::runtime_error("invalid --request: " + *v); }
            continue;
        }
        if (auto v = take("--user")) { o.user_filter = *v; continue; }

        if (a == "--available") { o.available = true; continue; }

        if (!a.empty() && a[0] == '-')
            throw std::runtime_error("unknown flag: " + a);
        o.positional.push_back(a);
    }
    return o;
}

// --- ошибки ---

int fail(const std::string& msg, bool json_out, const char* code = "book_error") {
    if (json_out) {
        json j;
        j["ok"] = false;
        j["error"]["code"] = code;
        j["error"]["message"] = msg;
        std::fprintf(stderr, "%s\n", j.dump().c_str());
    } else {
        std::fprintf(stderr, "libcore: %s\n", msg.c_str());
    }
    return 1;
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

std::string fmt_date(long long t) {
    auto s = core::load_settings();
    return util::format_date(t, s.time_format);
}

// --- book add ---

int do_add(const BookOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        if (o.title.value_or("").empty() || o.author.value_or("").empty()) {
            return fail("--title and --author are required for remote", json_out);
        }
        json args;
        args["title"]  = *o.title;
        args["author"] = *o.author;
        if (o.year) args["year"] = *o.year;
        if (o.isbn) args["isbn"] = *o.isbn;
        args["count"]  = o.count.value_or(1);
        return remote_call(*prof, "book.add", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    if (!role_at_least(ctx.role, "moderator"))
        return fail("requires moderator role or higher", json_out, "forbidden");

    std::string title  = o.title.value_or("");
    std::string author = o.author.value_or("");
    std::optional<int> year = o.year;
    std::optional<std::string> isbn = o.isbn;
    int count = o.count.value_or(1);

    if (title.empty()) {
        if (json_out) return fail("--title is required in --json mode", json_out);
        std::fprintf(stderr, "Название: ");
        std::fflush(stderr);
        std::getline(std::cin, title);
    }
    if (title.empty()) return fail("title is empty", json_out);

    if (author.empty()) {
        if (json_out) return fail("--author is required in --json mode", json_out);
        std::fprintf(stderr, "Автор: ");
        std::fflush(stderr);
        std::getline(std::cin, author);
    }
    if (author.empty()) return fail("author is empty", json_out);

    if (!year && !json_out) {
        std::fprintf(stderr, "Год (Enter — пропустить): ");
        std::fflush(stderr);
        std::string line;
        std::getline(std::cin, line);
        if (!line.empty()) {
            try { year = std::stoi(line); } catch (...) {}
        }
    }

    if (!isbn && !json_out) {
        std::fprintf(stderr, "ISBN (Enter — пропустить): ");
        std::fflush(stderr);
        std::string line;
        std::getline(std::cin, line);
        if (!line.empty()) isbn = core::normalize_isbn(line);
    } else if (isbn) {
        isbn = core::normalize_isbn(*isbn);
    }

    if (!o.count && !json_out) {
        std::fprintf(stderr, "Копий [1]: ");
        std::fflush(stderr);
        std::string line;
        std::getline(std::cin, line);
        if (!line.empty()) {
            try { count = std::stoi(line); } catch (...) {}
        }
    }
    if (count < 1) count = 1;

    auto res = core::add_book(database, title, author, year, isbn, ctx.user_id);
    auto copies = core::add_copies(database, res.book_id, count);

    core::log_info(database, "book.added", ctx.user_id,
                   std::string("{\"book_id\":") + std::to_string(res.book_id) +
                   ",\"title\":\"" + title + "\"}");

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["book_id"]  = res.book_id;
        j["data"]["created"]  = res.created;
        j["data"]["copies"]   = copies.size();
        j["data"]["copy_ids"] = copies;
        j["data"]["title"]    = title;
        j["data"]["author"]   = author;
        std::printf("%s\n", j.dump().c_str());
    } else {
        if (res.created) {
            std::printf("Книга добавлена: \"%s\" — %s (id=%lld)\n",
                        title.c_str(), author.c_str(), res.book_id);
        } else {
            std::printf("Книга уже была: \"%s\" — %s (id=%lld)\n",
                        title.c_str(), author.c_str(), res.book_id);
        }
        std::printf("Добавлено копий: %zu (id: ", copies.size());
        for (std::size_t i = 0; i < copies.size(); ++i) {
            std::printf("%s%lld", i ? ", " : "", copies[i]);
        }
        std::printf(")\n");
    }
    return 0;
}

// --- book info ---

int do_info(const BookOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        json args;
        if (o.id) {
            args["id"] = *o.id;
        } else {
            std::string q;
            for (std::size_t i = 0; i < o.positional.size(); ++i) {
                if (i) q += " ";
                q += o.positional[i];
            }
            args["query"] = q;
        }
        return remote_call(*prof, "book.info", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);

    if (o.id) {
        auto b = core::find_by_id(database, *o.id);
        if (!b) return fail("book not found: id=" + std::to_string(*o.id), json_out, "not_found");

        auto copies = core::list_copies(database, *o.id);
        int avail = core::count_available(database, *o.id);

        if (json_out) {
            json j;
            j["ok"] = true;
            j["data"]["id"]        = b->id;
            j["data"]["title"]     = b->title;
            j["data"]["author"]    = b->author;
            j["data"]["year"]      = b->year ? json(*b->year) : json(nullptr);
            j["data"]["isbn"]      = b->isbn ? json(*b->isbn) : json(nullptr);
            j["data"]["added_at"]  = b->added_at;
            j["data"]["added_by"]  = b->added_by ? json(*b->added_by) : json(nullptr);
            j["data"]["available"] = avail;
            json cj = json::array();
            for (const auto& c : copies) {
                json one;
                one["id"]       = c.id;
                one["status"]   = c.status;
                one["added_at"] = c.added_at;
                cj.push_back(one);
            }
            j["data"]["copies"] = cj;
            std::printf("%s\n", j.dump().c_str());
        } else {
            std::printf("\"%s\" — %s\n", b->title.c_str(), b->author.c_str());
            std::printf("  id:   %lld\n", b->id);
            if (b->year) std::printf("  год:  %d\n", *b->year);
            if (b->isbn) std::printf("  ISBN: %s\n", b->isbn->c_str());

            std::printf("Копии:\n");
            for (const auto& c : copies) {
                std::printf("  [%lld] %s\n", c.id, c.status.c_str());
            }
            std::printf("Доступно: %d из %zu\n", avail, copies.size());
        }
        return 0;
    }

    std::string query;
    for (std::size_t i = 0; i < o.positional.size(); ++i) {
        if (i) query += " ";
        query += o.positional[i];
    }

    auto books = core::search_books(database, query);
    if (books.empty()) return fail("not found: " + query, json_out, "not_found");

    if (json_out) {
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
        json j;
        j["ok"] = true;
        j["data"]["books"] = arr;
        std::printf("%s\n", j.dump().c_str());
    } else {
        for (const auto& b : books) {
            std::printf("  [%lld] \"%s\" — %s", b.id, b.title.c_str(), b.author.c_str());
            if (b.year) std::printf(" (%d)", *b.year);
            if (b.isbn) std::printf(" ISBN:%s", b.isbn->c_str());
            std::printf("\n");
        }
    }
    return 0;
}

// --- book list ---

int do_list(const BookOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        json args;
        args["available"] = o.available;
        return remote_call(*prof, "book.list", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);
    auto books = core::list_books(database);

    if (o.available) {
        std::vector<core::Book> filtered;
        for (const auto& b : books) {
            if (core::count_available(database, b.id) > 0)
                filtered.push_back(b);
        }
        books = std::move(filtered);
    }

    if (json_out) {
        json arr = json::array();
        for (const auto& b : books) {
            json one;
            one["id"]        = b.id;
            one["title"]     = b.title;
            one["author"]    = b.author;
            one["year"]      = b.year ? json(*b.year) : json(nullptr);
            one["isbn"]      = b.isbn ? json(*b.isbn) : json(nullptr);
            one["available"] = core::count_available(database, b.id);
            arr.push_back(one);
        }
        json j;
        j["ok"] = true;
        j["data"]["books"] = arr;
        std::printf("%s\n", j.dump().c_str());
    } else {
        if (books.empty()) {
            std::printf("Книг нет.\n");
        } else {
            std::printf("Книги (%zu):\n", books.size());
            for (const auto& b : books) {
                int av = core::count_available(database, b.id);
                std::printf("  [%lld] \"%s\" — %s", b.id, b.title.c_str(), b.author.c_str());
                if (b.year) std::printf(" (%d)", *b.year);
                if (b.isbn) std::printf(" ISBN:%s", b.isbn->c_str());
                std::printf("  [%d доступно]\n", av);
            }
        }
    }
    return 0;
}

// --- book grant ---

int do_grant(const BookOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        json args;
        if (!o.positional.empty()) {
            try { args["book_id"] = std::stoll(o.positional[0]); }
            catch (...) { return fail("invalid ID", json_out); }
        }
        if (o.to) args["to"] = *o.to;
        if (o.until) args["until"] = *o.until;
        return remote_call(*prof, "book.grant", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    if (!role_at_least(ctx.role, "moderator"))
        return fail("requires moderator role or higher", json_out, "forbidden");

    if (o.positional.empty()) {
        return fail("usage: libcore book grant <bookID|copyID> --to <user> [--until DATE]", json_out);
    }
    if (!o.to || o.to->empty()) {
        return fail("--to <username> is required", json_out);
    }

    long long arg_id;
    try { arg_id = std::stoll(o.positional[0]); }
    catch (...) { return fail("invalid ID: " + o.positional[0], json_out); }

    long long book_id = 0;
    long long forced_copy = 0;

    auto b = core::find_by_id(database, arg_id);
    if (b) {
        book_id = arg_id;
    } else {
        auto cr = database.query_params(
            "SELECT book_id FROM copies WHERE id = ? LIMIT 1;",
            { db::int_cell(arg_id) }
        );
        if (cr.empty()) return fail("no book or copy with id=" + std::to_string(arg_id), json_out, "not_found");
        book_id = db::row_int(cr[0], 0);
        forced_copy = arg_id;
    }

    auto ur = database.query_params(
        "SELECT id FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(*o.to) }
    );
    if (ur.empty()) return fail("user not found: " + *o.to, json_out, "user_not_found");
    long long user_id = db::row_int(ur[0], 0);

    std::optional<long long> until_ts;
    if (o.until && !o.until->empty()) {
        auto s = core::load_settings();
        auto ts = util::parse_date(*o.until, s.time_format);
        if (!ts) return fail("invalid --until date (expected " + s.time_format + ")", json_out);
        until_ts = *ts;
    }

    if (forced_copy) {
        auto status_r = database.query_params(
            "SELECT status FROM copies WHERE id = ? LIMIT 1;",
            { db::int_cell(forced_copy) }
        );
        if (status_r.empty()) return fail("copy not found", json_out);
        if (db::row_text(status_r[0], 0) != "available") {
            auto alt = core::find_available_copy(database, book_id);
            if (alt) {
                return fail("copy " + std::to_string(forced_copy) +
                            " is not available; try copy " + std::to_string(*alt), json_out, "copy_busy");
            }
            return fail("copy " + std::to_string(forced_copy) + " is not available", json_out, "copy_busy");
        }
    }

    try {
        long long loan_id;
        if (forced_copy) {
            auto now = static_cast<long long>(std::time(nullptr));
            std::optional<long long> enddate = until_ts;
            if (!enddate) {
                auto s = core::load_settings();
                if (s.loan_days > 0) {
                    enddate = now + static_cast<long long>(s.loan_days) * 24 * 3600;
                }
            }
            database.begin();
            try {
                database.exec_params(
                    "INSERT INTO loans (copy_id, book_id, user_id, givedate, enddate) "
                    "VALUES (?, ?, ?, ?, ?);",
                    {
                        db::int_cell(forced_copy),
                        db::int_cell(book_id),
                        db::int_cell(user_id),
                        db::int_cell(now),
                        enddate ? db::int_cell(*enddate) : db::null_cell()
                    }
                );
                loan_id = database.last_insert_rowid();
                database.exec_params(
                    "UPDATE copies SET status = 'loaned' WHERE id = ?;",
                    { db::int_cell(forced_copy) }
                );
                database.exec_params(
                    "UPDATE requests SET status = 'approved' "
                    "WHERE book_id = ? AND user_id = ? AND status = 'pending';",
                    { db::int_cell(book_id), db::int_cell(user_id) }
                );
                database.commit();
            } catch (...) {
                database.rollback();
                throw;
            }
        } else {
            loan_id = core::grant_loan(database, book_id, user_id, until_ts);
        }

        auto active = core::active_loans_for_book(database, book_id);
        std::optional<long long> actual_enddate;
        long long actual_copy = 0;
        for (const auto& l : active) {
            if (l.user_id == user_id) {
                actual_copy = l.copy_id;
                actual_enddate = l.enddate;
                break;
            }
        }

        core::log_info(database, "book.granted", ctx.user_id,
                       std::string("{\"book_id\":") + std::to_string(book_id) +
                       ",\"to\":\"" + *o.to + "\"}");

        if (json_out) {
            json j;
            j["ok"] = true;
            j["data"]["loan_id"]  = loan_id;
            j["data"]["book_id"]  = book_id;
            j["data"]["copy_id"]  = actual_copy;
            j["data"]["user"]     = *o.to;
            j["data"]["givedate"] = static_cast<long long>(std::time(nullptr));
            j["data"]["enddate"]  = actual_enddate ? json(*actual_enddate) : json(nullptr);
            std::printf("%s\n", j.dump().c_str());
        } else {
            std::printf("Выдано: \"%s\" → %s\n",
                        b ? b->title.c_str() : "?",
                        o.to->c_str());
            std::printf("  копия: %lld\n", actual_copy);
            if (actual_enddate) {
                std::printf("  вернуть до: %s\n", fmt_date(*actual_enddate).c_str());
            } else {
                std::printf("  бессрочно\n");
            }
        }
        return 0;
    } catch (const std::exception& e) {
        return fail(e.what(), json_out);
    }
}

// --- book return ---

int do_return(const BookOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        json args;
        if (!o.positional.empty()) {
            try { args["copy_id"] = std::stoll(o.positional[0]); }
            catch (...) { return fail("invalid copy ID", json_out); }
        }
        return remote_call(*prof, "book.return", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    if (!role_at_least(ctx.role, "moderator"))
        return fail("requires moderator role or higher", json_out, "forbidden");

    if (o.positional.empty()) {
        return fail("usage: libcore book return <copyID>", json_out);
    }

    long long copy_id;
    try { copy_id = std::stoll(o.positional[0]); }
    catch (...) { return fail("invalid copy ID: " + o.positional[0], json_out); }

    bool ok = core::return_loan(database, copy_id);
    if (!ok) {
        if (json_out) {
            json j;
            j["ok"] = false;
            j["error"]["code"] = "not_loaned";
            j["error"]["message"] = "copy is already on the shelf";
            std::fprintf(stderr, "%s\n", j.dump().c_str());
        } else {
            std::printf("Копия %lld и так на полке.\n", copy_id);
        }
        return 1;
    }

    core::log_info(database, "book.returned", ctx.user_id,
                   std::string("{\"copy_id\":") + std::to_string(copy_id) + "}");

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["copy_id"] = copy_id;
        j["data"]["returned_at"] = static_cast<long long>(std::time(nullptr));
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("Копия %lld возвращена.\n", copy_id);
    }
    return 0;
}

// --- book owner ---

int do_owner(const BookOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        json args;
        if (!o.positional.empty()) {
            try { args["book_id"] = std::stoll(o.positional[0]); }
            catch (...) { return fail("invalid book ID", json_out); }
        }
        return remote_call(*prof, "book.owner", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);

    if (o.positional.empty()) {
        return fail("usage: libcore book owner <bookID>", json_out);
    }
    long long book_id;
    try { book_id = std::stoll(o.positional[0]); }
    catch (...) { return fail("invalid book ID: " + o.positional[0], json_out); }

    auto b = core::find_by_id(database, book_id);
    if (!b) return fail("book not found: id=" + std::to_string(book_id), json_out, "not_found");

    auto loans = core::active_loans_for_book(database, book_id);

    if (json_out) {
        json arr = json::array();
        for (const auto& l : loans) {
            auto ur = database.query_params(
                "SELECT username FROM users WHERE id = ? LIMIT 1;",
                { db::int_cell(l.user_id) }
            );
            json one;
            one["copy_id"]  = l.copy_id;
            one["user"]     = ur.empty() ? "?" : db::row_text(ur[0], 0);
            one["givedate"] = l.givedate;
            one["enddate"]  = l.enddate ? json(*l.enddate) : json(nullptr);
            arr.push_back(one);
        }
        json j;
        j["ok"] = true;
        j["data"]["book_id"] = book_id;
        j["data"]["active_loans"] = arr;
        std::printf("%s\n", j.dump().c_str());
    } else {
        if (loans.empty()) {
            std::printf("Все копии \"%s\" на полке.\n", b->title.c_str());
        } else {
            std::printf("У кого \"%s\":\n", b->title.c_str());
            for (const auto& l : loans) {
                auto ur = database.query_params(
                    "SELECT username FROM users WHERE id = ? LIMIT 1;",
                    { db::int_cell(l.user_id) }
                );
                std::string uname = ur.empty() ? "?" : db::row_text(ur[0], 0);
                std::printf("  [%lld] у %s (выдана %s",
                            l.copy_id, uname.c_str(), fmt_date(l.givedate).c_str());
                if (l.enddate) std::printf(", вернуть до %s", fmt_date(*l.enddate).c_str());
                std::printf(")\n");
            }
        }
    }
    return 0;
}

// --- book givedate ---

int do_givedate(const BookOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        json args;
        if (!o.positional.empty()) {
            try { args["copy_id"] = std::stoll(o.positional[0]); }
            catch (...) { return fail("invalid copy ID", json_out); }
        }
        return remote_call(*prof, "book.givedate", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);

    if (o.positional.empty()) return fail("usage: libcore book givedate <copyID>", json_out);
    long long copy_id;
    try { copy_id = std::stoll(o.positional[0]); }
    catch (...) { return fail("invalid copy ID", json_out); }

    auto loan = core::active_loan_for_copy(database, copy_id);
    if (!loan) return fail("copy " + std::to_string(copy_id) + " is not loaned", json_out, "not_loaned");

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["copy_id"]  = copy_id;
        j["data"]["givedate"] = loan->givedate;
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("Выдана: %s\n", fmt_date(loan->givedate).c_str());
    }
    return 0;
}

// --- book enddate ---

int do_enddate(const BookOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        json args;
        if (!o.positional.empty()) {
            try { args["copy_id"] = std::stoll(o.positional[0]); }
            catch (...) { return fail("invalid copy ID", json_out); }
        }
        return remote_call(*prof, "book.enddate", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);

    if (o.positional.empty()) return fail("usage: libcore book enddate <copyID>", json_out);
    long long copy_id;
    try { copy_id = std::stoll(o.positional[0]); }
    catch (...) { return fail("invalid copy ID", json_out); }

    auto loan = core::active_loan_for_copy(database, copy_id);
    if (!loan) return fail("copy " + std::to_string(copy_id) + " is not loaned", json_out, "not_loaned");

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["copy_id"] = copy_id;
        j["data"]["enddate"] = loan->enddate ? json(*loan->enddate) : json(nullptr);
        std::printf("%s\n", j.dump().c_str());
    } else {
        if (loan->enddate) std::printf("Вернуть до: %s\n", fmt_date(*loan->enddate).c_str());
        else               std::printf("Бессрочно\n");
    }
    return 0;
}

// --- book request ---

int do_request(const BookOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        json args;
        if (!o.positional.empty()) {
            try { args["book_id"] = std::stoll(o.positional[0]); }
            catch (...) { return fail("invalid book ID", json_out); }
        }
        return remote_call(*prof, "book.request", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    if (o.positional.empty()) return fail("usage: libcore book request <bookID> [--until DATE]", json_out);
    long long book_id;
    try { book_id = std::stoll(o.positional[0]); }
    catch (...) { return fail("invalid book ID", json_out); }

    auto b = core::find_by_id(database, book_id);
    if (!b) return fail("book not found: id=" + std::to_string(book_id), json_out, "not_found");

    try {
        long long req_id = core::create_request(database, book_id, ctx.user_id);
        core::log_info(database, "book.requested", ctx.user_id,
                       std::string("{\"book_id\":") + std::to_string(book_id) +
                       ",\"request_id\":" + std::to_string(req_id) + "}");
        if (json_out) {
            json j;
            j["ok"] = true;
            j["data"]["request_id"] = req_id;
            j["data"]["book_id"]    = book_id;
            j["data"]["status"]     = "pending";
            std::printf("%s\n", j.dump().c_str());
        } else {
            std::printf("Запрос создан (id=%lld) на \"%s\".\n",
                        req_id, b->title.c_str());
        }
        return 0;
    } catch (const std::exception& e) {
        return fail(e.what(), json_out);
    }
}

// --- book wanted ---

int do_wanted(const BookOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        json args;
        if (o.id) args["book_id"] = *o.id;
        if (o.user_filter) args["user"] = *o.user_filter;
        return remote_call(*prof, "book.wanted", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    if (!role_at_least(ctx.role, "moderator"))
        return fail("requires moderator role or higher", json_out, "forbidden");

    std::optional<long long> book_id = o.id;
    std::optional<long long> user_id;
    if (o.user_filter) {
        auto ur = database.query_params(
            "SELECT id FROM users WHERE username = ? LIMIT 1;",
            { db::text_cell(*o.user_filter) }
        );
        if (!ur.empty()) user_id = db::row_int(ur[0], 0);
    }

    auto reqs = core::list_requests(database, book_id, user_id, "pending");

    if (json_out) {
        json arr = json::array();
        for (const auto& r : reqs) {
            auto ur = database.query_params("SELECT username FROM users WHERE id = ? LIMIT 1;",
                                            { db::int_cell(r.user_id) });
            auto br = database.query_params("SELECT title FROM books WHERE id = ? LIMIT 1;",
                                            { db::int_cell(r.book_id) });
            json one;
            one["id"]         = r.id;
            one["book_id"]    = r.book_id;
            one["book"]       = br.empty() ? "?" : db::row_text(br[0], 0);
            one["user"]       = ur.empty() ? "?" : db::row_text(ur[0], 0);
            one["created_at"] = r.created_at;
            arr.push_back(one);
        }
        json j;
        j["ok"] = true;
        j["data"]["requests"] = arr;
        std::printf("%s\n", j.dump().c_str());
    } else {
        if (reqs.empty()) {
            std::printf("Запросов нет.\n");
        } else {
            std::printf("Запросы (%zu):\n", reqs.size());
            for (const auto& r : reqs) {
                auto ur = database.query_params("SELECT username FROM users WHERE id = ? LIMIT 1;",
                                                { db::int_cell(r.user_id) });
                auto br = database.query_params("SELECT title FROM books WHERE id = ? LIMIT 1;",
                                                { db::int_cell(r.book_id) });
                std::printf("  [%lld] %s → \"%s\" (%s)\n",
                            r.id,
                            ur.empty() ? "?" : db::row_text(ur[0], 0).c_str(),
                            br.empty() ? "?" : db::row_text(br[0], 0).c_str(),
                            fmt_date(r.created_at).c_str());
            }
        }
    }
    return 0;
}

} // namespace

int cmd_book(const std::vector<std::string>& args, bool json_out) {
    if (args.empty()) return fail("usage: libcore book <add|info|list|grant|return|owner|givedate|enddate|request|wanted>", json_out);

    std::string sub = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());

    BookOpts o;
    try { o = parse_opts(rest); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }

    if (sub == "add")      return do_add(o, json_out);
    if (sub == "info")     return do_info(o, json_out);
    if (sub == "list")     return do_list(o, json_out);
    if (sub == "grant")    return do_grant(o, json_out);
    if (sub == "return")   return do_return(o, json_out);
    if (sub == "owner")    return do_owner(o, json_out);
    if (sub == "givedate") return do_givedate(o, json_out);
    if (sub == "enddate")  return do_enddate(o, json_out);
    if (sub == "request")  return do_request(o, json_out);
    if (sub == "wanted")   return do_wanted(o, json_out);

    return fail("unknown subcommand: book " + sub, json_out);
}

} // namespace libcore::handlers
