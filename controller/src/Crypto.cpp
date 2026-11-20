#include "cricket/controller/Crypto.h"

#include <cstring>
#include <fstream>
#include <random>
#include <stdexcept>

namespace cricket::ctl {

namespace {

inline uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t Rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

// Pads per FIPS 180-4: 0x80, zeros, 64-bit big-endian bit length.
std::string Pad(const std::string& data) {
    std::string m = data;
    const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    m += static_cast<char>(0x80);
    while (m.size() % 64 != 56) m += '\0';
    for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (i * 8)) & 0xff);
    return m;
}

uint32_t Be32(const std::string& m, size_t at) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(m[at])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(m[at + 1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(m[at + 2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(m[at + 3]));
}

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

std::array<uint8_t, 32> Sha256(const std::string& data) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const std::string m = Pad(data);
    uint32_t w[64];
    for (size_t chunk = 0; chunk < m.size(); chunk += 64) {
        for (int i = 0; i < 16; ++i) w[i] = Be32(m, chunk + i * 4);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<uint8_t>(h[i] >> (24 - 8 * j));
    return out;
}

std::array<uint8_t, 20> Sha1(const std::string& data) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    const std::string m = Pad(data);
    uint32_t w[80];
    for (size_t chunk = 0; chunk < m.size(); chunk += 64) {
        for (int i = 0; i < 16; ++i) w[i] = Be32(m, chunk + i * 4);
        for (int i = 16; i < 80; ++i) w[i] = Rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
            else f = b ^ c ^ d, k = 0xCA62C1D6;
            uint32_t t = Rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = Rotl(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::array<uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<uint8_t>(h[i] >> (24 - 8 * j));
    return out;
}

std::string HmacSha256(const std::string& key, const std::string& message) {
    std::string k = key;
    if (k.size() > 64) {
        auto d = Sha256(k);
        k.assign(reinterpret_cast<const char*>(d.data()), d.size());
    }
    k.resize(64, '\0');
    std::string ipad(64, '\0'), opad(64, '\0');
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<char>(k[i] ^ 0x36);
        opad[i] = static_cast<char>(k[i] ^ 0x5c);
    }
    auto inner = Sha256(ipad + message);
    auto outer = Sha256(opad + std::string(reinterpret_cast<const char*>(inner.data()), inner.size()));
    return std::string(reinterpret_cast<const char*>(outer.data()), outer.size());
}

std::string HmacHex(const std::string& key, const std::string& message) { return ToHex(HmacSha256(key, message)); }

std::string ToHex(const std::string& bytes) {
    static const char* d = "0123456789abcdef";
    std::string o;
    o.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        o += d[c >> 4];
        o += d[c & 15];
    }
    return o;
}

std::string Base64(const std::string& bytes) {
    std::string o;
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        uint32_t v = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8) |
                     static_cast<uint8_t>(bytes[i + 2]);
        o += kB64[(v >> 18) & 63];
        o += kB64[(v >> 12) & 63];
        o += kB64[(v >> 6) & 63];
        o += kB64[v & 63];
    }
    if (i + 1 == bytes.size()) {
        uint32_t v = static_cast<uint8_t>(bytes[i]) << 16;
        o += kB64[(v >> 18) & 63];
        o += kB64[(v >> 12) & 63];
        o += "==";
    } else if (i + 2 == bytes.size()) {
        uint32_t v = (static_cast<uint8_t>(bytes[i]) << 16) | (static_cast<uint8_t>(bytes[i + 1]) << 8);
        o += kB64[(v >> 18) & 63];
        o += kB64[(v >> 12) & 63];
        o += kB64[(v >> 6) & 63];
        o += '=';
    }
    return o;
}

std::string Base64Url(const std::string& bytes) {
    std::string s = Base64(bytes);
    while (!s.empty() && s.back() == '=') s.pop_back();
    for (char& c : s) c = c == '+' ? '-' : c == '/' ? '_' : c;
    return s;
}

bool Base64UrlDecode(const std::string& text, std::string* out) {
    out->clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '-') v = 62;
        else if (c == '_') v = 63;
        else return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            *out += static_cast<char>((acc >> bits) & 0xff);
        }
    }
    // Leftover bits must be zero padding (canonical encoding only).
    return (acc & ((1u << bits) - 1)) == 0 && text.size() % 4 != 1;
}

bool ConstantTimeEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

RandomSource SystemRandom() {
    return [](size_t n) {
        std::string out(n, '\0');
        std::ifstream f("/dev/urandom", std::ios::binary);
        if (f && f.read(out.data(), static_cast<std::streamsize>(n))) return out;
        // Fallback: the platform's random_device (secure on mainstream platforms).
        std::random_device rd;
        for (size_t i = 0; i < n; ++i) out[i] = static_cast<char>(rd() & 0xff);
        return out;
    };
}

}  // namespace cricket::ctl
