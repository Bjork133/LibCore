#pragma once

#include "api/common.hpp"

#include <optional>
#include <string>

namespace libcore::api {

// book_list: {available?: bool} → {books: [...]}
json book_list(const std::string& library, const std::string& token,
               bool available_only);

// book_info: либо {id: int}, либо {query: string}
json book_info_by_id(const std::string& library, long long id);
json book_info_by_query(const std::string& library, const std::string& query);

// book_add: {title, author, year?, isbn?, count} (moderator+)
json book_add(const std::string& library, const std::string& token,
              const std::string& title,
              const std::string& author,
              std::optional<int> year,
              std::optional<std::string> isbn,
              int count);

// book_grant: {book_id, to, until?} (moderator+)
json book_grant(const std::string& library, const std::string& token,
                long long book_id,
                const std::string& to,
                std::optional<std::string> until);

// book_return: {copy_id} (moderator+)
json book_return(const std::string& library, const std::string& token,
                 long long copy_id);

// book_owner: {book_id} → {active_loans}
json book_owner(const std::string& library, long long book_id);

// book_givedate / book_enddate: {copy_id}
json book_givedate(const std::string& library, long long copy_id);
json book_enddate(const std::string& library, long long copy_id);

// book_request: {book_id}
json book_request(const std::string& library, const std::string& token,
                  long long book_id);

// book_wanted: {book_id?, user?}
json book_wanted(const std::string& library, const std::string& token,
                 std::optional<long long> book_id,
                 std::optional<std::string> user);

} // namespace libcore::api
