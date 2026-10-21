#pragma once

#include <filesystem>

namespace libcore::paths {

// ~/.config/libcore/
std::filesystem::path config_dir();

// ~/.local/share/libcore/
std::filesystem::path data_dir();

// ~/.config/libcore/help/
std::filesystem::path help_dir();

// Создаёт все нужные папки (config, data, help)
void ensure_dirs();

} // namespace libcore::paths
