#pragma once

#include <string>

namespace libcore::core {

struct Settings {
    std::string time_format = "DD.MM.YYYY";
    int         loan_days   = 30;
};

// ~/.config/libcore/settings.json
Settings load_settings();
void     save_settings(const Settings& s);

// Создаёт settings.json с дефолтами, если файла нет.
void ensure_settings_file();

} // namespace libcore::core
