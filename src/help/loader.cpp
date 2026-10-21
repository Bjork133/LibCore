#include "help/loader.hpp"

#include "embedded_help.hpp"
#include "util/file.hpp"
#include "util/paths.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace libcore::help {

namespace {

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

void install_embedded_if_missing() {
    auto dir = paths::help_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    for (const auto& e : embedded()) {
        auto target = dir / (std::string(e.name) + ".txt");
        if (util::file_exists(target)) continue;
        util::write_file(target, e.text);
    }
}

std::optional<std::string> find(std::string_view name) {
    std::string lc = to_lower(name);
    auto path = paths::help_dir() / (lc + ".txt");

    if (auto content = util::read_file(path)) {
        return content;
    }

    // fallback — вшитый
    for (const auto& e : embedded()) {
        if (e.name == lc) {
            return std::string(e.text);
        }
    }
    return std::nullopt;
}

std::string search_path_for(std::string_view name) {
    std::string lc = to_lower(name);
    return (paths::help_dir() / (lc + ".txt")).string();
}

} // namespace libcore::help
