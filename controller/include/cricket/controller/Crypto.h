// Small, dependency-free crypto helpers for the phone controller link:
// SHA-256 / HMAC-SHA256 (message authentication), SHA-1 (only for the
// WebSocket handshake, as the WebSocket standard requires), base64 and a
// secure random source. Verified against the standard test vectors.
//
// Phone browsers do not offer their built-in crypto (WebCrypto) on plain
// http:// local-network pages, so the phone side uses a matching
// JavaScript implementation (controller/web/protocol.js) checked against
// the same vectors.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace cricket::ctl {

std::array<uint8_t, 32> Sha256(const std::string& data);
std::array<uint8_t, 20> Sha1(const std::string& data);
// Raw 32-byte MAC.
std::string HmacSha256(const std::string& key, const std::string& message);
// Lower-case hex of HMAC-SHA256 (what goes on the wire).
std::string HmacHex(const std::string& key, const std::string& message);

std::string ToHex(const std::string& bytes);
std::string Base64(const std::string& bytes);
std::string Base64Url(const std::string& bytes);  // no padding (safe in URLs)
bool Base64UrlDecode(const std::string& text, std::string* out);

// Compares without leaking timing information about where they differ.
bool ConstantTimeEqual(const std::string& a, const std::string& b);

// Random bytes. The default reads the operating system's secure source;
// tests inject a deterministic one.
using RandomSource = std::function<std::string(size_t)>;
RandomSource SystemRandom();

}  // namespace cricket::ctl
