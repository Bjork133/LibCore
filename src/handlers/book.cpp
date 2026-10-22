#include "handlers/book.hpp"

#include "api/book.hpp"
#include "api/common.hpp"
#include "api/library.hpp"
#include "core/profiles.hpp"
#include "handlers/remote.hpp"

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

// --- контекст профиля ---

struct Ctx {
    std::string profile_name;
    std::string library;
    std::string username;
    std::string token;
    std::string mode;
    std::optional<std::string> host;
    std::optional<int> port;
    std::optional<std::string> code;
};

std::optional<Ctx> load_ctx(const std::optional<std::string>& explicit_profile) {
    std::string profile_name = explicit_profile.value_or("");
    auto all = core::load_profiles();
    if (profile_name.empty()) profile_name = all.default_name;
    if (profile_name.empty()) return std::nullopt;

    auto prof = core::get_profile(profile_name);
    if (!prof) return std::nullopt;

    Ctx c;
    c.profile_name = prof->name;
    c.library      = prof->library;
    c.username     = prof->username;
    c.token        = prof->session_token.value_or("");
    c.mode         = prof->mode;
    c.host         = prof->host;
    c.port         = prof->port;
    c.code         = prof->code;
    return c;
}

core::Profile profile_from_ctx(const Ctx& c) {
    core::Profile p;
    p.name          = c.profile_name;
    p.library       = c.library;
    p.username      = c.username;
    p.mode          = c.mode;
    p.host          = c.host;
    p.port          = c.port;
    p.code          = c.code;
    if (!c.token.empty()) p.session_token = c.token;
    return p;
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
        if (auto v = take("--user")) { o.user_filter = *v; continue; }

        if (a == "--available") { o.available = true; continue; }

        if (!a.empty() && a[0] == '-')
            throw std::runtime_error("unknown flag: " + a);
        o.positional.push_back(a);
    }
    return o;
}

// --- вывод ---

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

// Вызов API: либо локально, либо через remote.
json call_api(const Ctx& c, const std::string& cmd, const json& args, bool json_out) {
    if (c.mode == "remote") {
        try {
            auto p = profile_from_ctx(c);
            return remote_raw(p, cmd, args);
        } catch (const std::exception& e) {
            // имитируем api-ответ
            json j;
            j["ok"] = false;
            j["error"]["code"] = "network";
            j["error"]["message"] = e.what();
            return j;
        }
    }

    // локально: вызываем api
    if (cmd == "book.list")
        return api::book_list(c.library, c.token, args.value("available", false));
    if (cmd == "book.info") {
        if (args.contains("id") && !args["id"].is_null())
            return api::book_info_by_id(c.library, args["id"].get<long long>());
        return api::book_info_by_query(c.library, args.value("query", ""));
    }
    if (cmd == "book.add") {
        std::optional<int> year;
        if (args.contains("year") && !args["year"].is_null()) year = args["year"].get<int>();
        std::optional<std::string> isbn;
        if (args.contains("isbn") && !args["isbn"].is_null()) isbn = args["isbn"].get<std::string>();
        return api::book_add(c.library, c.token,
            args.value("title", ""),
            args.value("author", ""),
            year, isbn,
            args.value("count", 1));
    }
    if (cmd == "book.grant") {
        std::optional<std::string> until;
        if (args.contains("until") && !args["until"].is_null()) until = args["until"].get<std::string>();
        return api::book_grant(c.library, c.token,
            args["book_id"].get<long long>(),
            args["to"].get<std::string>(),
            until);
    }
    if (cmd == "book.return")
        return api::book_return(c.library, c.token, args["copy_id"].get<long long>());
    if (cmd == "book.owner")
        return api::book_owner(c.library, args["book_id"].get<long long>());
    if (cmd == "book.givedate")
        return api::book_givedate(c.library, args["copy_id"].get<long long>());
    if (cmd == "book.enddate")
        return api::book_enddate(c.library, args["copy_id"].get<long long>());
    if (cmd == "book.request")
        return api::book_request(c.library, c.token, args["book_id"].get<long long>());
    if (cmd == "book.wanted") {
        std::optional<long long> book_id;
        std::optional<std::string> user;
        if (args.contains("book_id") && !args["book_id"].is_null())
            book_id = args["book_id"].get<long long>();
        if (args.contains("user") && !args["user"].is_null())
            user = args["user"].get<std::string>();
        return api::book_wanted(c.library, c.token, book_id, user);
    }

    json j;
    j["ok"] = false;
    j["error"]["code"] = "unknown_cmd";
    j["error"]["message"] = cmd;
    return j;
}

// Печать результата
int print_result(const json& resp, bool json_out) {
    if (!resp.value("ok", false)) {
        std::string code = resp["error"].value("code", "unknown");
        std::string msg  = resp["error"].value("message", "");
        if (json_out) {
            std::fprintf(stderr, "%s\n", resp.dump().c_str());
        } else {
            std::fprintf(stderr, "libcore: %s\n", msg.c_str());
        }
        return 1;
    }

    if (json_out) {
        std::printf("%s\n", resp.dump().c_str());
    } else {
        // human-вывод — по ключам
        auto d = resp["data"];
        if (d.contains("books")) {
            auto& arr = d["books"];
            if (arr.empty()) std::printf("Книг нет.\n");
            else {
                std::printf("Книги (%zu):\n", arr.size());
                for (const auto& b : arr) {
                    std::printf("  [%lld] \"%s\" — %s",
                                b["id"].get<long long>(),
                                b["title"].get<std::string>().c_str(),
                                b["author"].get<std::string>().c_str());
                    if (!b["year"].is_null()) std::printf(" (%d)", b["year"].get<int>());
                    if (!b["isbn"].is_null()) std::printf(" ISBN:%s", b["isbn"].get<std::string>().c_str());
                    if (b.contains("available")) std::printf("  [%d доступно]", b["available"].get<int>());
                    std::printf("\n");
                }
            }
        } else if (d.contains("copies")) {
            std::printf("\"%s\" — %s\n", d["title"].get<std::string>().c_str(),
                        d["author"].get<std::string>().c_str());
            std::printf("  id: %lld\n", d["id"].get<long long>());
            if (!d["year"].is_null()) std::printf("  год: %d\n", d["year"].get<int>());
            if (!d["isbn"].is_null()) std::printf("  ISBN: %s\n", d["isbn"].get<std::string>().c_str());
            std::printf("Копии:\n");
            for (const auto& c : d["copies"]) {
                std::printf("  [%lld] %s\n", c["id"].get<long long>(),
                            c["status"].get<std::string>().c_str());
            }
            if (d.contains("available"))
                std::printf("Доступно: %d из %zu\n",
                            d["available"].get<int>(), d["copies"].size());
        } else if (d.contains("book_id") && d.contains("copies")) {
            std::printf("Книга: id=%lld, создано=%s, копий=%d\n",
                        d["book_id"].get<long long>(),
                        d["created"].get<bool>() ? "да" : "нет",
                        d["copies"].get<int>());
        } else if (d.contains("loan_id")) {
            std::printf("Выдано: \"book\" → %s (loan_id=%lld)\n",
                        d["to"].get<std::string>().c_str(),
                        d["loan_id"].get<long long>());
        } else if (d.contains("copy_id") && d.contains("returned_at")) {
            std::printf("Копия %lld возвращена.\n", d["copy_id"].get<long long>());
        } else if (d.contains("copy_id") && d.contains("givedate")) {
            std::printf("Выдана: ts=%lld\n", d["givedate"].get<long long>());
        } else if (d.contains("copy_id") && d.contains("enddate")) {
            if (d["enddate"].is_null()) std::printf("Бессрочно\n");
            else std::printf("Вернуть до: ts=%lld\n", d["enddate"].get<long long>());
        } else if (d.contains("request_id")) {
            std::printf("Запрос создан (id=%lld).\n", d["request_id"].get<long long>());
        } else if (d.contains("requests")) {
            auto& arr = d["requests"];
            if (arr.empty()) std::printf("Запросов нет.\n");
            else {
                std::printf("Запросы (%zu):\n", arr.size());
                for (const auto& r : arr) {
                    std::printf("  [%lld] %s → \"%s\"\n",
                                r["id"].get<long long>(),
                                r["user"].get<std::string>().c_str(),
                                r["book"].get<std::string>().c_str());
                }
            }
        } else if (d.contains("active_loans")) {
            auto& arr = d["active_loans"];
            if (arr.empty()) std::printf("Все копии на полке.\n");
            else {
                std::printf("У кого:\n");
                for (const auto& l : arr) {
                    std::printf("  [%lld] у %s\n",
                                l["copy_id"].get<long long>(),
                                l["user"].get<std::string>().c_str());
                }
            }
        } else {
            // generic: напечатать JSON
            std::printf("%s\n", d.dump(2).c_str());
        }
    }
    return 0;
}

// --- команды ---

int do_list(const BookOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    json args;
    args["available"] = o.available;
    auto resp = call_api(*c_opt, "book.list", args, json_out);
    return print_result(resp, json_out);
}

int do_info(const BookOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    json args;
    if (o.id) args["id"] = *o.id;
    else {
        std::string q;
        for (std::size_t i = 0; i < o.positional.size(); ++i) {
            if (i) q += " ";
            q += o.positional[i];
        }
        args["query"] = q;
    }
    auto resp = call_api(*c_opt, "book.info", args, json_out);
    return print_result(resp, json_out);
}

int do_add(const BookOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");

    std::string title  = o.title.value_or("");
    std::string author = o.author.value_or("");
    std::optional<int> year = o.year;
    std::optional<std::string> isbn = o.isbn;
    int count = o.count.value_or(1);

    if (c_opt->mode == "remote") {
        if (title.empty() || author.empty())
            return fail("--title and --author are required for remote", json_out);
    } else {
        if (title.empty()) {
            if (json_out) return fail("--title is required in --json mode", json_out);
            std::fprintf(stderr, "Название: "); std::fflush(stderr);
            std::getline(std::cin, title);
        }
        if (title.empty()) return fail("title is empty", json_out);
        if (author.empty()) {
            if (json_out) return fail("--author is required in --json mode", json_out);
            std::fprintf(stderr, "Автор: "); std::fflush(stderr);
            std::getline(std::cin, author);
        }
        if (author.empty()) return fail("author is empty", json_out);
        if (!year && !json_out) {
            std::fprintf(stderr, "Год (Enter — пропустить): "); std::fflush(stderr);
            std::string line; std::getline(std::cin, line);
            if (!line.empty()) { try { year = std::stoi(line); } catch (...) {} }
        }
        if (!isbn && !json_out) {
            std::fprintf(stderr, "ISBN (Enter — пропустить): "); std::fflush(stderr);
            std::string line; std::getline(std::cin, line);
            if (!line.empty()) isbn = line;
        }
        if (!o.count && !json_out) {
            std::fprintf(stderr, "Копий [1]: "); std::fflush(stderr);
            std::string line; std::getline(std::cin, line);
            if (!line.empty()) { try { count = std::stoi(line); } catch (...) {} }
        }
    }
    if (count < 1) count = 1;

    json args;
    args["title"]  = title;
    args["author"] = author;
    if (year) args["year"] = *year;
    if (isbn) args["isbn"] = *isbn;
    args["count"] = count;

    auto resp = call_api(*c_opt, "book.add", args, json_out);
    return print_result(resp, json_out);
}

int do_grant(const BookOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    if (o.positional.empty()) return fail("usage: book grant <bookID> --to <user> [--until DATE]", json_out);
    if (!o.to || o.to->empty()) return fail("--to <username> is required", json_out);

    long long book_id;
    try { book_id = std::stoll(o.positional[0]); }
    catch (...) { return fail("invalid ID", json_out); }

    json args;
    args["book_id"] = book_id;
    args["to"]      = *o.to;
    if (o.until) args["until"] = *o.until;
    auto resp = call_api(*c_opt, "book.grant", args, json_out);
    return print_result(resp, json_out);
}

int do_return(const BookOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    if (o.positional.empty()) return fail("usage: book return <copyID>", json_out);
    long long copy_id;
    try { copy_id = std::stoll(o.positional[0]); }
    catch (...) { return fail("invalid copy ID", json_out); }

    json args;
    args["copy_id"] = copy_id;
    auto resp = call_api(*c_opt, "book.return", args, json_out);
    return print_result(resp, json_out);
}

int do_owner(const BookOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    if (o.positional.empty()) return fail("usage: book owner <bookID>", json_out);
    long long book_id;
    try { book_id = std::stoll(o.positional[0]); }
    catch (...) { return fail("invalid book ID", json_out); }

    json args;
    args["book_id"] = book_id;
    auto resp = call_api(*c_opt, "book.owner", args, json_out);
    return print_result(resp, json_out);
}

int do_givedate(const BookOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    if (o.positional.empty()) return fail("usage: book givedate <copyID>", json_out);
    long long copy_id;
    try { copy_id = std::stoll(o.positional[0]); }
    catch (...) { return fail("invalid copy ID", json_out); }

    json args;
    args["copy_id"] = copy_id;
    auto resp = call_api(*c_opt, "book.givedate", args, json_out);
    return print_result(resp, json_out);
}

int do_enddate(const BookOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    if (o.positional.empty()) return fail("usage: book enddate <copyID>", json_out);
    long long copy_id;
    try { copy_id = std::stoll(o.positional[0]); }
    catch (...) { return fail("invalid copy ID", json_out); }

    json args;
    args["copy_id"] = copy_id;
    auto resp = call_api(*c_opt, "book.enddate", args, json_out);
    return print_result(resp, json_out);
}

int do_request(const BookOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    if (o.positional.empty()) return fail("usage: book request <bookID>", json_out);
    long long book_id;
    try { book_id = std::stoll(o.positional[0]); }
    catch (...) { return fail("invalid book ID", json_out); }

    json args;
    args["book_id"] = book_id;
    auto resp = call_api(*c_opt, "book.request", args, json_out);
    return print_result(resp, json_out);
}

int do_wanted(const BookOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");

    json args;
    if (o.id) args["book_id"] = *o.id;
    if (o.user_filter) args["user"] = *o.user_filter;
    auto resp = call_api(*c_opt, "book.wanted", args, json_out);
    return print_result(resp, json_out);
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
