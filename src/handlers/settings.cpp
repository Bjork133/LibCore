#include "handlers/settings.hpp"

#include "core/settings.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>

namespace libcore::handlers {

using json = nlohmann::json;

namespace {

struct SettingsOpts {
    std::vector<std::string> positional;
};

SettingsOpts parse_opts(const std::vector<std::string>& args) {
    SettingsOpts o;
    for (const auto& a : args) {
        if (!a.empty() && a[0] == '-')
            throw std::runtime_error("unknown flag: " + a);
        o.positional.push_back(a);
    }
    return o;
}

int fail(const std::string& msg, bool json_out, const char* code = "settings_error") {
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

// --- settings show ---

int do_show(bool json_out) {
    auto s = core::load_settings();

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["time_format"] = s.time_format;
        j["data"]["loan_days"]   = s.loan_days;
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("Настройки:\n");
        std::printf("  time_format: %s\n", s.time_format.c_str());
        std::printf("  loan_days:   %d\n", s.loan_days);
    }
    return 0;
}

// --- settings time-format [PATTERN] ---

int do_time_format(const SettingsOpts& o, bool json_out) {
    auto s = core::load_settings();

    if (o.positional.empty()) {
        // показать
        if (json_out) {
            json j;
            j["ok"] = true;
            j["data"]["time_format"] = s.time_format;
            std::printf("%s\n", j.dump().c_str());
        } else {
            std::printf("%s\n", s.time_format.c_str());
        }
        return 0;
    }

    std::string pattern = o.positional[0];

    // Мягкая валидация: должно быть Y, M, D
    bool has_y = false, has_m = false, has_d = false;
    for (char c : pattern) {
        if (c == 'Y') has_y = true;
        else if (c == 'M') has_m = true;
        else if (c == 'D') has_d = true;
    }
    if (!has_y || !has_m || !has_d) {
        return fail("pattern must contain Y, M and D (e.g. YYYY-MM-DD)", json_out);
    }

    s.time_format = pattern;
    core::save_settings(s);

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["time_format"] = s.time_format;
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("time_format = %s\n", s.time_format.c_str());
    }
    return 0;
}

// --- settings loan-days [N] ---

int do_loan_days(const SettingsOpts& o, bool json_out) {
    auto s = core::load_settings();

    if (o.positional.empty()) {
        if (json_out) {
            json j;
            j["ok"] = true;
            j["data"]["loan_days"] = s.loan_days;
            std::printf("%s\n", j.dump().c_str());
        } else {
            std::printf("%d\n", s.loan_days);
        }
        return 0;
    }

    int n;
    try { n = std::stoi(o.positional[0]); }
    catch (...) { return fail("invalid number: " + o.positional[0], json_out); }

    if (n < 0) return fail("loan_days cannot be negative", json_out);

    s.loan_days = n;
    core::save_settings(s);

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["loan_days"] = s.loan_days;
        std::printf("%s\n", j.dump().c_str());
    } else {
        if (n == 0) std::printf("loan_days = 0 (бессрочно)\n");
        else        std::printf("loan_days = %d\n", n);
    }
    return 0;
}

} // namespace

int cmd_settings(const std::vector<std::string>& args, bool json_out) {
    if (args.empty()) {
        return fail("usage: libcore settings <show|time-format|loan-days>", json_out);
    }

    std::string sub = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());

    SettingsOpts o;
    try { o = parse_opts(rest); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }

    if (sub == "show")        return do_show(json_out);
    if (sub == "time-format") return do_time_format(o, json_out);
    if (sub == "loan-days")   return do_loan_days(o, json_out);

    return fail("unknown subcommand: settings " + sub, json_out);
}

} // namespace libcore::handlers
