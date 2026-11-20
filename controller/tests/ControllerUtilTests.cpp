// Crypto, encoding, JSON and QR utilities for the phone controller.
// Crypto is checked against the official test vectors; QR output against
// matrices cross-checked with two independent reference encoders.
#include "../../core/tests/Test.h"
#include "cricket/controller/Crypto.h"
#include "cricket/controller/Json.h"
#include "cricket/controller/QrCode.h"

using namespace cricket::ctl;

namespace {
std::string H(const std::array<uint8_t, 32>& d) { return ToHex(std::string(d.begin(), d.end())); }
std::string H1(const std::array<uint8_t, 20>& d) { return ToHex(std::string(d.begin(), d.end())); }
}  // namespace

TEST(ctl_sha256_fips_vectors) {
    CHECK_EQ(H(Sha256("")), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(H(Sha256("abc")), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(H(Sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK_EQ(H(Sha256(std::string(1000000, 'a'))),
             std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

TEST(ctl_sha1_and_websocket_accept) {
    CHECK_EQ(H1(Sha1("")), std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
    CHECK_EQ(H1(Sha1("abc")), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
    // RFC 6455 section 1.3 example.
    auto d = Sha1("dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    CHECK_EQ(Base64(std::string(d.begin(), d.end())), std::string("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
}

TEST(ctl_hmac_rfc4231_vectors) {
    CHECK_EQ(HmacHex(std::string(20, '\x0b'), "Hi There"),
             std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
    CHECK_EQ(HmacHex("Jefe", "what do ya want for nothing?"),
             std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    CHECK_EQ(HmacHex(std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First"),
             std::string("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
}

TEST(ctl_base64_rfc4648_and_url_safe) {
    const char* in[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char* out[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    for (int i = 0; i < 7; ++i) CHECK_EQ(Base64(in[i]), std::string(out[i]));
    std::string bytes;
    for (int i = 0; i < 256; ++i) bytes += static_cast<char>(i);
    const std::string u = Base64Url(bytes);
    CHECK(u.find_first_of("+/=") == std::string::npos);
    std::string back;
    CHECK(Base64UrlDecode(u, &back));
    CHECK(back == bytes);
    CHECK(!Base64UrlDecode("ab+c", &back));  // not URL-safe alphabet
    CHECK(!Base64UrlDecode("abcde", &back)); // impossible length
    CHECK(!Base64UrlDecode("Zh", &back));    // non-canonical leftover bits
    CHECK(ConstantTimeEqual("abc", "abc"));
    CHECK(!ConstantTimeEqual("abc", "abd"));
    CHECK(!ConstantTimeEqual("abc", "abcd"));
    auto rnd = SystemRandom();
    CHECK_EQ(rnd(32).size(), size_t{32});
    CHECK(rnd(32) != rnd(32));
}

TEST(ctl_json_round_trip_and_access) {
    std::string err;
    auto j = Json::Parse(R"( {"t":"in","s":42,"neg":-1.5e2,"ok":true,"n":null,"a":[1,"x",{"k":[]}],
                         "u":"\u0915\u093e \ud83c\udfcf","esc":"q\"\\\/\n"} )", &err);
    CHECK(j.has_value());
    if (!j) return;
    CHECK_EQ(j->Str("t"), std::string("in"));
    CHECK_EQ(j->Int("s"), int64_t{42});
    CHECK_EQ(j->Num("neg"), -150.0);
    CHECK_EQ(j->Int("neg", 7), int64_t{-150});
    CHECK(j->Flag("ok"));
    CHECK_EQ(j->Get("a")->Items().size(), size_t{3});
    CHECK_EQ(j->Str("u"), std::string("का 🏏"));
    CHECK_EQ(j->Str("esc"), std::string("q\"\\/\n"));
    CHECK_EQ(j->Int("missing", 9), int64_t{9});
    CHECK_EQ(j->Int("t", 9), int64_t{9});  // wrong type
    auto again = Json::Parse(j->Dump());
    CHECK(again.has_value());
    if (again) CHECK_EQ(again->Dump(), j->Dump());
    Json b = Json::Object();
    b.Set("x", 1).Set("y", "two").Set("z", true).Set("x", 3);
    CHECK_EQ(b.Dump(), std::string(R"({"x":3,"y":"two","z":true})"));
    CHECK_EQ(Json::Number(0.5).Dump(), std::string("0.5"));
}

TEST(ctl_json_rejects_bad_input) {
    for (const char* bad : {"", "{", "{\"a\":1,}", "[1,2", "{\"a\" 1}", "tru", "01", "1.", "-", "\"abc",
                            "{\"a\":1}x", "{\"a\":1,\"a\":2}", "\"\\x\"", "\"\\ud800\"", "\"\\udc00\"",
                            "\"line\nbreak\"", "nan", "1e999"}) {
        std::string err;
        auto r = Json::Parse(bad, &err);
        if (r) std::cerr << "  accepted bad JSON: " << bad << "\n";
        CHECK(!r.has_value());
        CHECK(!err.empty());
    }
    std::string deep(40, '[');
    deep += std::string(40, ']');
    CHECK(!Json::Parse(deep).has_value());           // nested too deeply
    CHECK(Json::Parse("[[[[1]]]]").has_value());
}

TEST(ctl_qr_matches_reference_encoders) {
    // Hashes of matrices that were checked module-for-module against the
    // `qrcode` (forced mask) and `qrcodegen` (automatic mask) libraries.
    struct Case {
        std::string text;
        int mask, version;
        const char* sha256;
    } cases[] = {
        {"hello", 3, 1, "5f6bc25d90793bd8146bdb1ee677004c9c67f1513d49bcdda5521273ba80681f"},
        {"http://192.168.100.200:47321/#p=Q2xKa9ZtR0wq.Zk3c5a2Lr0mNq8vB7xY1pT4dW6eH9jU2oS0iA5gK3lFc", 5, 6,
         "f8cf250f10318f7fb12cfae60e2ff2895f6a649eae9fdd7bc9be916e173ff25c"},
        {std::string(213, 's'), 0, 10, "9a4f623e8d963d7e07f4080053c693af6fdf1efd187b91840201376288549802"},
        {std::string(70, 'q'), -1, 5, "8b8b872cbcd0700048c165f1f148c994671e03bc3025a543949a8dc63082aba3"},
        {"भारत pairing ✓", 2, 2, "0275fa3a6b59c7e1bd7afb09a6dff4a4c5c0de8f0a418bdcc617d3bbdf0930a3"},
        {"http://192.168.1.20:47321/#p=Ab3dEf9h.", -1, 3, "5c385a3af84e837a13cad4c7005f596cfa788af00bef7b0965a33859b54e5679"},
        {std::string(106, 'w'), 7, 6, "fcd4e5d406f2391d9f020c314c48c4f8cb80b82f18737f0dcab030fa7ac9225d"},
    };
    for (const auto& c : cases) {
        auto q = QrCode::Encode(c.text, c.mask);
        CHECK(q.has_value());
        if (!q) continue;
        CHECK_EQ(q->Version(), c.version);
        CHECK_EQ(q->Size(), c.version * 4 + 17);
        CHECK_EQ(H(Sha256(q->ToAscii())), std::string(c.sha256));
    }
}

TEST(ctl_qr_limits_and_renderings) {
    CHECK(!QrCode::Encode(std::string(214, 'x')).has_value());  // beyond version 10-M
    CHECK(!QrCode::Encode("x", 8).has_value());
    auto q = QrCode::Encode("http://192.168.1.20:47321/");
    CHECK(q.has_value());
    if (!q) return;
    CHECK(q->Mask() >= 0 && q->Mask() <= 7);
    // Finder pattern corners are dark, separators light.
    CHECK(q->Dark(0, 0) && q->Dark(6, 6) && !q->Dark(7, 7));
    const std::string svg = q->ToSvg(4);
    CHECK(svg.rfind("<svg", 0) == 0 && svg.find("</svg>") != std::string::npos);
    const std::string term = q->ToTerminal();
    int lines = 0;
    for (char c : term) lines += c == '\n';
    CHECK_EQ(lines, (q->Size() + 8 + 1) / 2);  // two rows per line, 4-module quiet zone
}
