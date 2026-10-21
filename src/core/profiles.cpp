#include "core/profiles.hpp"

#include "util/file.hpp"
#include "util/paths.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>

namespace libcore::core {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
fs::path profiles_path() {
    return paths::config_dir() / "profiles.json";
}
} // namespace

Profiles load_profiles() {
    Profiles p;
    auto content = util::read_file(profiles_path());
    if (!content) return p;

    try {
        auto j = json::parse(*content);
        if (j.contains("default") && !j["default"].is_null()) {
            p.default_name = j["default"].get<std::string>();
        }
        if (j.contains("profiles") && j["profiles"].is_object()) {
            for (auto& [key, v] : j["profiles"].items()) {
                Profile pr;
                pr.name     = key;
                pr.library  = v.value("library", "");
                pr.username = v.value("username", "");
                pr.mode     = v.value("mode", "local");

                if (v.contains("host") && !v["host"].is_null())
                    pr.host = v["host"].get<std::string>();
                if (v.contains("port") && !v["port"].is_null())
                    pr.port = v["port"].get<int>();
                if (v.contains("code") && !v["code"].is_null())
                    pr.code = v["code"].get<std::string>();
                if (v.contains("session_token") && !v["session_token"].is_null())
                    pr.session_token = v["session_token"].get<std::string>();

                p.all.push_back(std::move(pr));
            }
        }
    } catch (...) {
        // битый файл — возвращаем пустое
    }
    return p;
}

void save_profiles(const Profiles& p) {
    json j;
    j["default"] = p.default_name.empty() ? json(nullptr) : json(p.default_name);
    j["profiles"] = json::object();

    for (const auto& pr : p.all) {
        json v;
        v["name"]     = pr.name;
        v["library"]  = pr.library;
        v["username"] = pr.username;
        v["mode"]     = pr.mode;
        v["host"]     = pr.host ? json(*pr.host) : json(nullptr);
        v["port"]     = pr.port ? json(*pr.port) : json(nullptr);
        v["code"]     = pr.code ? json(*pr.code) : json(nullptr);
        if (pr.session_token) v["session_token"] = *pr.session_token;

        j["profiles"][pr.name] = v;
    }

    util::write_file(profiles_path(), j.dump(2) + "\n");
}

std::optional<Profile> get_profile(const std::string& name) {
    auto p = load_profiles();
    for (const auto& pr : p.all) {
        if (pr.name == name) return pr;
    }
    return std::nullopt;
}

void put_profile(const Profile& pr) {
    auto p = load_profiles();
    for (auto& x : p.all) {
        if (x.name == pr.name) {
            x = pr;
            save_profiles(p);
            return;
        }
    }
    p.all.push_back(pr);
    if (p.default_name.empty()) p.default_name = pr.name;
    save_profiles(p);
}

bool remove_profile(const std::string& name) {
    auto p = load_profiles();
    auto it = std::find_if(p.all.begin(), p.all.end(),
        [&](const Profile& x) { return x.name == name; });
    if (it == p.all.end()) return false;

    p.all.erase(it);
    if (p.default_name == name) {
        p.default_name = p.all.empty() ? "" : p.all.front().name;
    }
    save_profiles(p);
    return true;
}

void set_default_profile(const std::string& name) {
    auto p = load_profiles();
    for (const auto& x : p.all) {
        if (x.name == name) {
            p.default_name = name;
            save_profiles(p);
            return;
        }
    }
}

} // namespace libcore::core
