#include "api/book.hpp"

#include "api/library.hpp"
#include "core/books.hpp"
#include "core/loans.hpp"
#include "core/logs.hpp"
#include "core/requests.hpp"
#include "core/settings.hpp"
#include "util/dateparse.hpp"

#include <ctime>

namespace libcore::api {

namespace {

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

} // namespace

json book_list(const std::string& library, const std::string& token,
               bool available_only) {
    try {
        auto database = open_library(library);

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
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json book_info_by_id(const std::string& library, long long id) {
    try {
        auto database = open_library(library);
        auto b = core::find_by_id(database, id);
        if (!b) return err("not_found", "book not found: id=" + std::to_string(id));

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
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json book_info_by_query(const std::string& library, const std::string& query) {
    try {
        auto database = open_library(library);
        auto books = core::search_books(database, query);
        if (books.empty()) return err("not_found", "not found: " + query);

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
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json book_add(const std::string& library, const std::string& token,
              const std::string& title,
              const std::string& author,
              std::optional<int> year,
              std::optional<std::string> isbn,
              int count) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        if (!role_at_least(ctx->role, "moderator"))
            return err("forbidden", "requires moderator role or higher");

        if (title.empty() || author.empty())
            return err("bad_request", "title, author required");

        if (count < 1) count = 1;
        if (isbn) isbn = core::normalize_isbn(*isbn);

        auto res = core::add_book(database, title, author, year, isbn, ctx->user_id);
        auto copies = core::add_copies(database, res.book_id, count);

        core::log_info(database, "book.added", ctx->user_id,
                       std::string("{\"book_id\":") + std::to_string(res.book_id) +
                       ",\"title\":\"" + title + "\"}");

        json d;
        d["book_id"]  = res.book_id;
        d["created"]  = res.created;
        d["copies"]   = copies.size();
        d["copy_ids"] = copies;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json book_grant(const std::string& library, const std::string& token,
                long long book_id,
                const std::string& to,
                std::optional<std::string> until) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        if (!role_at_least(ctx->role, "moderator"))
            return err("forbidden", "requires moderator role or higher");

        auto ur = database.query_params(
            "SELECT id FROM users WHERE username = ? LIMIT 1;",
            { db::text_cell(to) }
        );
        if (ur.empty()) return err("user_not_found", "user not found: " + to);
        long long user_id = db::row_int(ur[0], 0);

        std::optional<long long> until_ts;
        if (until) {
            auto cfg = core::load_settings();
            auto ts = util::parse_date(*until, cfg.time_format);
            if (!ts) return err("bad_date", "invalid until date (expected " + cfg.time_format + ")");
            until_ts = *ts;
        }

        long long loan_id = core::grant_loan(database, book_id, user_id, until_ts);

        core::log_info(database, "book.granted", ctx->user_id,
                       std::string("{\"book_id\":") + std::to_string(book_id) +
                       ",\"to\":\"" + to + "\"}");

        json d;
        d["loan_id"] = loan_id;
        d["book_id"] = book_id;
        d["to"]      = to;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json book_return(const std::string& library, const std::string& token,
                 long long copy_id) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        if (!role_at_least(ctx->role, "moderator"))
            return err("forbidden", "requires moderator role or higher");

        bool ok_res = core::return_loan(database, copy_id);
        if (!ok_res) return err("not_loaned", "copy is not loaned");

        core::log_info(database, "book.returned", ctx->user_id,
                       std::string("{\"copy_id\":") + std::to_string(copy_id) + "}");

        json d;
        d["copy_id"] = copy_id;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json book_owner(const std::string& library, long long book_id) {
    try {
        auto database = open_library(library);
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
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json book_givedate(const std::string& library, long long copy_id) {
    try {
        auto database = open_library(library);
        auto loan = core::active_loan_for_copy(database, copy_id);
        if (!loan) return err("not_loaned", "copy is not loaned");

        json d;
        d["copy_id"]  = copy_id;
        d["givedate"] = loan->givedate;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json book_enddate(const std::string& library, long long copy_id) {
    try {
        auto database = open_library(library);
        auto loan = core::active_loan_for_copy(database, copy_id);
        if (!loan) return err("not_loaned", "copy is not loaned");

        json d;
        d["copy_id"] = copy_id;
        d["enddate"] = loan->enddate ? json(*loan->enddate) : json(nullptr);
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json book_request(const std::string& library, const std::string& token,
                  long long book_id) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        auto b = core::find_by_id(database, book_id);
        if (!b) return err("not_found", "book not found: id=" + std::to_string(book_id));

        long long req_id = core::create_request(database, book_id, ctx->user_id);
        core::log_info(database, "book.requested", ctx->user_id,
                       std::string("{\"book_id\":") + std::to_string(book_id) +
                       ",\"request_id\":" + std::to_string(req_id) + "}");

        json d;
        d["request_id"] = req_id;
        d["book_id"]    = book_id;
        d["status"]     = "pending";
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json book_wanted(const std::string& library, const std::string& token,
                 std::optional<long long> book_id,
                 std::optional<std::string> user) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        if (!role_at_least(ctx->role, "moderator"))
            return err("forbidden", "requires moderator role or higher");

        std::optional<long long> user_id;
        if (user) {
            auto ur = database.query_params(
                "SELECT id FROM users WHERE username = ? LIMIT 1;",
                { db::text_cell(*user) }
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
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

} // namespace libcore::api
