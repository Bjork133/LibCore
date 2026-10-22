#include "handlers/logs.hpp"

#include "api/common.hpp"
#include "api/logs.hpp"
#include "core/profiles.hpp"
#include "core/settings.hpp"
#include "handlers/remote.hpp"
#include "util/dateparse.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>

namespace libcore::handlers {

using json = nlohmann::json;

namespace {

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

struct LogOpts {
    std::optional<std::string> profile;
    std::optional<std::string> level;
    std::optional<std::string> event;
    std::optional<std::string> since;
    int tail = 50;
};

LogOpts parse_opts(const std::vector<std::string>& args) {
    LogOpts o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];

        auto take = [&](std::string_view flag) -> std::optional<std::string> {
            if (a.size() > flag.size() && a.starts_with(flag) && a[flag.size()] == '=')
                return a.substr(flag.size() + 1);
            if (a == flag) {
                if (i + 1 >= args.size())
                    throw std::runtime_error(std::string("flag ") + std::string(flag) + " requires a value");
                return args[++i];
            }
            return std::nullopt;
        };

        if (auto v = take("--profile")) { o.profile = *v; continue; }
        if (auto v = take("--level"))   { o.level   = *v; continue; }
        if (auto v = take("--event"))   { o.event   = *v; continue; }
        if (auto v = take("--since"))   { o.since   = *v; continue; }
        if (auto v = take("--tail")) {
            try { o.tail = std::stoi(*v); }
            catch (...) { throw std::runtime_error("invalid --tail: " + *v); }
            continue;
        }

        if (!a.empty() && a[0] == '-')
            throw std::runtime_error("unknown flag: " + a);
    }
    return o;
}

int fail(const std::string& msg, bool json_out, const char* code = "logs_error") {
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

} // namespace

int cmd_logs(const std::vector<std::string>& args, bool json_out) {
    LogOpts o;
    try { o = parse_opts(args); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }

    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto c = *c_opt;

    json args_j;
    if (o.level) args_j["level"] = *o.level;
    if (o.event) args_j["event"] = *o.event;
    if (o.since) args_j["since"] = *o.since;
    args_j["tail"] = o.tail;

    json resp;
    if (c.mode == "remote") {
        try {
            auto p = profile_from_ctx(c);
            resp = remote_raw(p, "logs", args_j);
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"]["code"] = "network";
            j["error"]["message"] = e.what();
            resp = j;
        }
    } else {
        resp = api::logs(c.library, c.token, o.level, o.event, o.since, o.tail);
    }

    if (!resp.value("ok", false)) {
        std::string msg = resp["error"].value("message", "");
        if (json_out) std::fprintf(stderr, "%s\n", resp.dump().c_str());
        else          std::fprintf(stderr, "libcore: %s\n", msg.c_str());
        return 1;
    }

    if (json_out) {
        std::printf("%s\n", resp.dump().c_str());
        return 0;
    }

    auto arr = resp["data"]["logs"];
    if (arr.empty()) {
        std::printf("Логов нет.\n");
        return 0;
    }
    auto s = core::load_settings();
    for (const auto& l : arr) {
        std::string ts = util::format_date(l["ts"].get<long long>(), s.time_format);
        std::printf("%s  %-5s  %-20s",
                    ts.c_str(),
                    l["level"].get<std::string>().c_str(),
                    l["event"].get<std::string>().c_str());
        if (!l["payload"].is_null())
            std::printf("  %s", l["payload"].get<std::string>().c_str());
        std::printf("\n");
    }
    return 0;
}

} // namespace libcore::handlers
