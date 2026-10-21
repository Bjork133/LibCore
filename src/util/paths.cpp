#include "util/paths.hpp"

#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
#  include <windows.h>
#  include <shlobj.h>
#endif

namespace libcore::paths {

namespace {

#ifdef _WIN32
std::filesystem::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (SHGetKnownFolderPath(id, 0, nullptr, &raw) != S_OK) {
        throw std::runtime_error("SHGetKnownFolderPath failed");
    }
    std::filesystem::path p(raw);
    CoTaskMemFree(raw);
    return p;
}
#else
std::filesystem::path home_dir() {
    const char* h = std::getenv("HOME");
    if (!h || !*h) throw std::runtime_error("HOME is not set");
    return std::filesystem::path(h);
}

std::filesystem::path xdg_config_home() {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    if (x && *x) return std::filesystem::path(x);
    return home_dir() / ".config";
}

std::filesystem::path xdg_data_home() {
    const char* x = std::getenv("XDG_DATA_HOME");
    if (x && *x) return std::filesystem::path(x);
    return home_dir() / ".local" / "share";
}
#endif

} // namespace

std::filesystem::path config_dir() {
#ifdef _WIN32
    return known_folder(FOLDERID_RoamingAppData) / "libcore";
#else
    return xdg_config_home() / "libcore";
#endif
}

std::filesystem::path data_dir() {
#ifdef _WIN32
    return known_folder(FOLDERID_LocalAppData) / "libcore";
#else
    return xdg_data_home() / "libcore";
#endif
}

std::filesystem::path help_dir() {
    return config_dir() / "help";
}

void ensure_dirs() {
    std::error_code ec;
    std::filesystem::create_directories(config_dir(), ec);
    std::filesystem::create_directories(data_dir(), ec);
    std::filesystem::create_directories(help_dir(), ec);
}

} // namespace libcore::paths
