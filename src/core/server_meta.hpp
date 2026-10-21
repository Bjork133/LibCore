#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace libcore::core {

struct ServerMeta {
    std::string name;
    std::string host;
    int         port = 8420;
    std::string code;
    long long   created_at = 0;
};

std::filesystem::path libraries_root();
std::filesystem::path library_dir(const std::string& name);
std::filesystem::path library_db_path(const std::string& name);
std::filesystem::path library_meta_path(const std::string& name);

std::vector<std::string> list_libraries();
bool library_exists(const std::string& name);
std::optional<ServerMeta> read_meta(const std::string& name);
void write_meta(const ServerMeta& meta);

std::string generate_code();

ServerMeta create_library(const std::string& name,
                          const std::string& host,
                          const std::string& password,
                          int port);

} // namespace libcore::core
