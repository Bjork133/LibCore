#include "core/settings.hpp"

#include "util/file.hpp"
#include "util/paths.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>

namespace libcore::core {

using json = nlohmann::json;

namespace {
std::filesystem::path settings_path() {
    return paths::config_dir() / "settings.json";
}
} // namespace

Settings load_settings() {
    Settings s;
    auto content = util::read_file(settings_path());
    if (!content) return s;

    try {
        auto j = json::parse(*content);
        s.time_format = j.value("time_format", s.time_format);
        s.loan_days   = j.value("loan_days",   s.loan_days);
    } catch (...) {
        // битый файл — возвращаем дефолты
    }
    return s;
}

void save_settings(const Settings& s) {
    json j;
    j["time_format"] = s.time_format;
    j["loan_days"]   = s.loan_days;
    util::write_file(settings_path(), j.dump(2) + "\n");
}

void ensure_settings_file() {
    if (!util::file_exists(settings_path())) {
        save_settings(Settings{});
    }
}

} // namespace libcore::core
