#include "api/logs.hpp"

#include "api/library.hpp"
#include "core/logs.hpp"
#include "core/settings.hpp"
#include "util/dateparse.hpp"

namespace libcore::api {

json logs(const std::string& library, const std::string& token,
          const std::optional<std::string>& level,
          const std::optional<std::string>& event,
          const std::optional<std::string>& since,
          int tail) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        if (!role_at_least(ctx->role, "moderator"))
            return err("forbidden", "requires moderator role or higher");

        std::optional<long long> since_ts;
        if (since) {
            auto s = core::load_settings();
            auto ts = util::parse_date(*since, s.time_format);
            if (!ts) return err("bad_date", "invalid since date (expected " + s.time_format + ")");
            since_ts = *ts;
        }

        auto entries = core::list_logs(database, level, event, since_ts, tail);

        json arr = json::array();
        for (const auto& l : entries) {
            json one;
            one["id"]      = l.id;
            one["ts"]      = l.ts;
            one["level"]   = l.level;
            one["event"]   = l.event;
            one["actor"]   = l.actor_id ? json(*l.actor_id) : json(nullptr);
            one["payload"] = l.payload ? json(*l.payload) : json(nullptr);
            arr.push_back(one);
        }
        json d;
        d["logs"] = arr;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

} // namespace libcore::api
