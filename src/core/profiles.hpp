#pragma once

#include <optional>
#include <string>
#include <vector>

namespace libcore::core {

struct Profile {
    std::string name;
    std::string library;
    std::string username;
    std::string mode;              // "local" | "remote"

    std::optional<std::string> host;
    std::optional<int>         port;
    std::optional<std::string> code;
    std::optional<std::string> session_token;
};

struct Profiles {
    std::vector<Profile> all;
    std::string          default_name;   // может быть пустым
};

// ~/.config/libcore/profiles.json
Profiles load_profiles();
void     save_profiles(const Profiles& p);

std::optional<Profile> get_profile(const std::string& name);
void                   put_profile(const Profile& p);
bool                   remove_profile(const std::string& name);
void                   set_default_profile(const std::string& name);


} // namespace libcore::core
