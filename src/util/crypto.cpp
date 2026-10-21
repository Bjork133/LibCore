#include "util/crypto.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <stdexcept>
#include <vector>

extern "C" {
    int libcore_scrypt(const uint8_t *pass, size_t passlen,
                       const uint8_t *salt, size_t saltlen,
                       uint64_t N, uint32_t r, uint32_t p,
                       uint8_t *out, size_t outlen);
}

namespace libcore::util {

namespace {

// --- Параметры scrypt по умолчанию ---
constexpr uint64_t kDefaultN = 65536;  // 2^16
constexpr uint32_t kDefaultR = 8;
constexpr uint32_t kDefaultP = 1;
constexpr std::size_t kSaltLen = 16;
constexpr std::size_t kHashLen = 64;   // 512 бит

// --- Base64 ---

constexpr char kB64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// --- Генерация соли ---
std::vector<unsigned char> random_salt() {
    std::vector<unsigned char> salt(kSaltLen);
    std::random_device rd;
    for (std::size_t i = 0; i < kSaltLen; i += 4) {
        uint32_t v = rd();
        std::memcpy(salt.data() + i, &v, std::min<std::size_t>(4, kSaltLen - i));
    }
    return salt;
}

// --- Парсинг "scrypt$N$r$p$salt$hash" ---

struct ParsedHash {
    uint64_t N = 0;
    uint32_t r = 0;
    uint32_t p = 0;
    std::string salt;   // сырые байты
    std::string hash;   // сырые байты
};

bool parse_hash(std::string_view stored, ParsedHash& out) {
    // Разбиваем по '$'
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= stored.size(); ++i) {
        if (i == stored.size() || stored[i] == '$') {
            parts.push_back(stored.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() != 6) return false;
    if (parts[0] != "scrypt") return false;

    try {
        out.N = std::stoull(std::string(parts[1]));
        out.r = static_cast<uint32_t>(std::stoul(std::string(parts[2])));
        out.p = static_cast<uint32_t>(std::stoul(std::string(parts[3])));
    } catch (...) {
        return false;
    }
    if (!base64_decode(parts[4], out.salt)) return false;
    if (!base64_decode(parts[5], out.hash)) return false;
    return true;
}

} // namespace

// --- base64 ---

std::string base64_encode(const unsigned char* data, std::size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (std::size_t i = 0; i < len; i += 3) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) v |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) v |= static_cast<uint32_t>(data[i + 2]);

        out.push_back(kB64Alphabet[(v >> 18) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? kB64Alphabet[(v >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? kB64Alphabet[v & 0x3F]        : '=');
    }
    return out;
}

std::string base64_encode(std::string_view s) {
    return base64_encode(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

bool base64_decode(std::string_view in, std::string& out) {
    out.clear();
    if (in.size() % 4 != 0) return false;

    out.reserve((in.size() / 4) * 3);

    for (std::size_t i = 0; i < in.size(); i += 4) {
        int v[4];
        int pad = 0;
        for (int j = 0; j < 4; ++j) {
            char c = in[i + j];
            if (c == '=') {
                v[j] = 0;
                ++pad;
            } else {
                v[j] = b64_value(c);
                if (v[j] < 0) return false;
            }
        }
        if (pad > 2) return false;

        uint32_t triple = (static_cast<uint32_t>(v[0]) << 18)
                        | (static_cast<uint32_t>(v[1]) << 12)
                        | (static_cast<uint32_t>(v[2]) << 6)
                        |  static_cast<uint32_t>(v[3]);

        out.push_back(static_cast<char>((triple >> 16) & 0xFF));
        if (pad < 2) out.push_back(static_cast<char>((triple >> 8) & 0xFF));
        if (pad < 1) out.push_back(static_cast<char>(triple & 0xFF));
    }
    return true;
}

// --- hash / verify ---

std::string hash_password(std::string_view plain) {
    auto salt = random_salt();

    std::array<unsigned char, kHashLen> hash{};
    int rc = libcore_scrypt(
        reinterpret_cast<const uint8_t*>(plain.data()), plain.size(),
        salt.data(), salt.size(),
        kDefaultN, kDefaultR, kDefaultP,
        hash.data(), hash.size()
    );
    if (rc != 0) {
        throw std::runtime_error("scrypt failed");
    }

    std::string out;
    out.reserve(128);
    out += "scrypt$";
    out += std::to_string(kDefaultN);
    out += "$";
    out += std::to_string(kDefaultR);
    out += "$";
    out += std::to_string(kDefaultP);
    out += "$";
    out += base64_encode(salt.data(), salt.size());
    out += "$";
    out += base64_encode(hash.data(), hash.size());
    return out;
}

bool verify_password(std::string_view plain, std::string_view stored) {
    ParsedHash parsed;
    if (!parse_hash(stored, parsed)) return false;

    std::array<unsigned char, kHashLen> computed{};
    // Используем длину хэша из stored — на случай, если она поменяется.
    std::size_t hash_len = std::min(parsed.hash.size(), computed.size());

    int rc = libcore_scrypt(
        reinterpret_cast<const uint8_t*>(plain.data()), plain.size(),
        reinterpret_cast<const uint8_t*>(parsed.salt.data()), parsed.salt.size(),
        parsed.N, parsed.r, parsed.p,
        computed.data(), hash_len
    );
    if (rc != 0) return false;

    // Константное по времени сравнение.
    if (hash_len != parsed.hash.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < hash_len; ++i) {
        diff |= computed[i] ^ static_cast<unsigned char>(parsed.hash[i]);
    }
    return diff == 0;
}

} // namespace libcore::util
