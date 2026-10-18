// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "UpdateTestSupport.h"

#include "core/FileSystem.h"
#include "updates/Encoding.h"
#include "updates/Sha256.h"
#include "updates/SignatureVerifier.h"
#include "updates/TrustedKeys.h"

#include <array>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::updates;
using namespace aether::updates::testing;

namespace {

std::vector<std::uint8_t> Bytes(std::string_view text) { return {text.begin(), text.end()}; }

std::string DigestHex(std::string_view data) {
    auto digest = Sha256::Hash(data);
    if (!digest) return "<error: " + digest.error().Describe() + ">";
    return ToHex(*digest);
}

std::vector<std::uint8_t> FixtureDer(const fixtures::SignedRelease& fixture) {
    auto der = encoding::DecodeBase64(fixture.derSignatureBase64);
    if (!der) {
        test::ReportFailure(__FILE__, __LINE__, "fixture DER signature is not base64");
        throw test::RequireFailure{};
    }
    return std::move(der).value();
}

std::string Base64(const std::vector<std::uint8_t>& bytes) { return encoding::EncodeBase64(bytes); }

}  // namespace

// ------------------------------------------------------------------------------------------ hex

AETHER_TEST("encoding: hex round-trips and rejects malformed text") {
    const std::vector<std::uint8_t> bytes{0x00, 0x01, 0x7F, 0x80, 0xAB, 0xFF};
    CHECK_EQ(encoding::ToHex(bytes), std::string("00017f80abff"));
    auto decoded = encoding::FromHex("00017F80abFF");
    REQUIRE(decoded.ok());
    CHECK(*decoded == bytes);
    CHECK(encoding::FromHex("").ok());
    CHECK_EQ(encoding::FromHex("abc").error().code, ErrorCode::Parse);
    CHECK_EQ(encoding::FromHex("0g").error().code, ErrorCode::Parse);
    CHECK_FALSE(encoding::FromHex(" 00").ok());
    CHECK_FALSE(encoding::FromHex("0x00").ok());
}

// --------------------------------------------------------------------------------------- base64

AETHER_TEST("encoding: base64 matches the RFC 4648 test vectors") {
    const std::array<std::pair<std::string_view, std::string_view>, 7> vectors{{{"", ""},
                                                                                {"f", "Zg=="},
                                                                                {"fo", "Zm8="},
                                                                                {"foo", "Zm9v"},
                                                                                {"foob", "Zm9vYg=="},
                                                                                {"fooba", "Zm9vYmE="},
                                                                                {"foobar", "Zm9vYmFy"}}};
    for (const auto& [plain, encoded] : vectors) {
        CHECK_EQ(encoding::EncodeBase64(Bytes(plain)), std::string(encoded));
        auto decoded = encoding::DecodeBase64(encoded);
        REQUIRE(decoded.ok());
        CHECK(*decoded == Bytes(plain));
    }
    std::vector<std::uint8_t> all(256);
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = static_cast<std::uint8_t>(i);
    auto roundTrip = encoding::DecodeBase64(encoding::EncodeBase64(all));
    REQUIRE(roundTrip.ok());
    CHECK(*roundTrip == all);
}

AETHER_TEST("encoding: base64 decoding is strict") {
    const std::vector<std::string_view> invalid{
        "Zg", "Zg=", "Zg===", "Z===", "====", "Zm9v====", "Zg==Zg==", "Zm9v\n", "Zm 9v", " Zm9v", "Zm9v\r\n",
        "Zm-v", "Zm_v", "Zh==", "Zm9=", "Zm=v", "Z=9v", "Zm9v YmFy", std::string_view("Zm9\0", 4),
        "Zm9v\xC3\xA9" "A"};
    for (const auto bad : invalid) {
        auto decoded = encoding::DecodeBase64(bad);
        CHECK_FALSE(decoded.ok());
        if (!decoded) CHECK_EQ(decoded.error().code, ErrorCode::Parse);
    }
}

// ----------------------------------------------------------------------------------- timestamps

AETHER_TEST("encoding: UTC timestamps round-trip and reject other forms") {
    using namespace std::chrono;
    const auto time = sys_days{year{2026} / 10 / 17} + hours{8} + minutes{5} + seconds{9};
    CHECK_EQ(encoding::FormatUtcTimestamp(time), std::string("2026-10-17T08:05:09Z"));
    auto parsed = encoding::ParseUtcTimestamp("2026-10-17T08:05:09Z");
    REQUIRE(parsed.ok());
    CHECK(*parsed == time);
    // Sub-second parts are truncated when formatting.
    CHECK_EQ(encoding::FormatUtcTimestamp(time + milliseconds{999}), std::string("2026-10-17T08:05:09Z"));
    auto leap = encoding::ParseUtcTimestamp("2028-02-29T23:59:59Z");
    CHECK(leap.ok());
    for (const std::string_view bad :
         {"2026-10-17T08:05:09+00:00", "2026-10-17 08:05:09Z", "2026-10-17T08:05:09", "2026-10-17T08:05:09.5Z",
          "2026-13-01T00:00:00Z", "2026-02-30T00:00:00Z", "2027-02-29T00:00:00Z", "2026-10-17T24:00:00Z",
          "2026-10-17T23:60:00Z", "2026-10-17T23:59:60Z", "1969-12-31T23:59:59Z", "2026-1O-17T08:05:09Z", ""}) {
        CHECK_FALSE(encoding::ParseUtcTimestamp(bad).ok());
    }
}

// -------------------------------------------------------------------------------------- SHA-256

AETHER_TEST("sha256: matches the FIPS 180 test vectors") {
    CHECK_EQ(DigestHex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(DigestHex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(DigestHex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK_EQ(DigestHex(std::string(1'000'000, 'a')),
             std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

AETHER_TEST("sha256: incremental hashing equals one-shot hashing and Finish resets the hasher") {
    const auto payload = TestAssetBytes(100'000, 9);
    auto oneShot = Sha256::Hash(payload);
    REQUIRE(oneShot.ok());

    auto hasher = Sha256::Create();
    REQUIRE(hasher.ok());
    std::size_t offset = 0;
    for (const std::size_t piece : {0u, 1u, 63u, 64u, 65u, 4096u, 31'337u}) {
        REQUIRE(hasher->Update(std::string_view(payload).substr(offset, piece)).ok());
        offset += piece;
    }
    REQUIRE(hasher->Update(std::string_view(payload).substr(offset)).ok());
    auto incremental = hasher->Finish();
    REQUIRE(incremental.ok());
    CHECK(*incremental == *oneShot);

    // The same object hashes a new message after Finish().
    REQUIRE(hasher->Update(std::string_view("abc")).ok());
    auto again = hasher->Finish();
    REQUIRE(again.ok());
    CHECK_EQ(ToHex(*again), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    Sha256 moved = std::move(hasher).value();
    CHECK(moved.Update(std::string_view("x")).ok());
}

AETHER_TEST("sha256: hashes files in blocks and observes cancellation") {
    const auto directory = test::MakeTempDirectory("updates-sha256");
    const auto file = directory / L"payload.bin";
    const auto payload = TestAssetBytes(3u * 1024 * 1024 + 17, 5);  // spans several 1 MiB blocks
    REQUIRE(fs::WriteFileAtomic(file, payload).ok());

    auto fromFile = Sha256::HashFile(file);
    auto fromMemory = Sha256::Hash(payload);
    REQUIRE(fromFile.ok());
    REQUIRE(fromMemory.ok());
    CHECK(*fromFile == *fromMemory);

    CancellationSource cancelled;
    cancelled.Cancel();
    auto aborted = Sha256::HashFile(file, cancelled.Token());
    REQUIRE_FALSE(aborted.ok());
    CHECK_EQ(aborted.error().code, ErrorCode::Cancelled);

    auto missing = Sha256::HashFile(directory / L"missing.bin");
    REQUIRE_FALSE(missing.ok());
    CHECK_EQ(missing.error().code, ErrorCode::NotFound);
}

AETHER_TEST("sha256: manifest digests must be exactly 64 lowercase hex characters") {
    const std::string valid = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    auto parsed = ParseSha256Hex(valid);
    REQUIRE(parsed.ok());
    CHECK_EQ(ToHex(*parsed), valid);
    CHECK_FALSE(ParseSha256Hex("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD").ok());
    CHECK_FALSE(ParseSha256Hex(valid.substr(1)).ok());
    CHECK_FALSE(ParseSha256Hex(valid + "0").ok());
    CHECK_FALSE(ParseSha256Hex("<release-generated-digest>").ok());
    CHECK_FALSE(ParseSha256Hex(std::string(63, 'a') + "g").ok());
}

// ------------------------------------------------------------------------------- ECDSA P-256

AETHER_TEST("ecdsa: the production verifier trusts exactly the compiled-in release key") {
    REQUIRE_EQ(trust::kReleaseSigningKeys.size(), 1u);
    CHECK_EQ(std::string(trust::kReleaseSigningKeys[0].publicKeyHex),
             std::string("2b0310c62dd1d8ec4e6b16c9caf44fced9f8df43f3807bc9d3e425ca28f4e12f"
                         "66b5ed190dad7d96f79405a82e7f5e60e5caa622a379bc19edfb42c92cb0ce02"));
    const auto production = EcdsaP256Verifier::ForReleaseManifests();
    CHECK_EQ(production.TrustedKeyCount(), 1u);  // the point is on the curve and imports
    // A manifest signed with the test key is not trusted by the production verifier.
    auto verdict =
        production.VerifySignatureFile(fixtures::kStable_1_2_0.manifest, fixtures::kStable_1_2_0.signature);
    REQUIRE_FALSE(verdict.ok());
    CHECK_EQ(verdict.error().code, ErrorCode::Signature);
}

AETHER_TEST("ecdsa: signatures made by sign_manifest.py verify (P1363 and DER)") {
    const auto verifier = TestVerifier();
    REQUIRE_EQ(verifier.TrustedKeyCount(), 1u);
    for (const auto* fixture :
         {&fixtures::kStable_1_2_0, &fixtures::kPrerelease_2_0_0_beta_2, &fixtures::kStable_1_3_0_MultiArch}) {
        auto p1363 = verifier.VerifySignatureFile(fixture->manifest, fixture->signature);
        REQUIRE(p1363.ok());
        CHECK_EQ(*p1363, std::string("test-key"));

        // The DER signature produced directly by "openssl dgst -sha256 -sign" is accepted too.
        auto der = verifier.VerifySignatureFile(fixture->manifest, fixture->derSignatureBase64);
        CHECK(der.ok());
        // One trailing CRLF is tolerated.
        CHECK(verifier.VerifySignatureFile(fixture->manifest, std::string(fixture->derSignatureBase64) + "\r\n").ok());
    }
}

AETHER_TEST("ecdsa: a tampered manifest, a wrong key or a modified signature are rejected") {
    const auto verifier = TestVerifier();
    const auto& fixture = fixtures::kStable_1_2_0;

    // Every single-byte change of the signed bytes must be detected; probe a spread of positions.
    for (std::size_t position = 0; position < fixture.manifest.size(); position += 37) {
        std::string tampered(fixture.manifest);
        tampered[position] = static_cast<char>(tampered[position] ^ 0x01);
        auto verdict = verifier.VerifySignatureFile(tampered, fixture.signature);
        CHECK_FALSE(verdict.ok());
        if (!verdict) CHECK_EQ(verdict.error().code, ErrorCode::Signature);
    }
    // Appending bytes (for example a reformatted file with an extra newline) breaks the signature.
    CHECK_FALSE(verifier.VerifySignatureFile(std::string(fixture.manifest) + "\n", fixture.signature).ok());
    // A signature over a different manifest does not transfer.
    CHECK_FALSE(verifier.VerifySignatureFile(fixture.manifest, fixtures::kPrerelease_2_0_0_beta_2.signature).ok());

    auto wrongKey = UnrelatedVerifier().VerifySignatureFile(fixture.manifest, fixture.signature);
    REQUIRE_FALSE(wrongKey.ok());
    CHECK_EQ(wrongKey.error().code, ErrorCode::Signature);

    auto raw = encoding::DecodeBase64(std::string_view(fixture.signature).substr(0, fixture.signature.size() - 1));
    REQUIRE(raw.ok());
    REQUIRE_EQ(raw->size(), 64u);
    for (const std::size_t index : {0u, 31u, 32u, 63u}) {
        auto flipped = *raw;
        flipped[index] ^= 0x80;
        auto verdict = verifier.VerifySignatureFile(fixture.manifest, Base64(flipped));
        CHECK_FALSE(verdict.ok());
        if (!verdict) CHECK_EQ(verdict.error().code, ErrorCode::Signature);
    }
}

AETHER_TEST("ecdsa: key rotation accepts any trusted key and reports which one verified") {
    const std::array keys{trust::TrustedKey{"old-key", fixtures::kUnrelatedPublicKeyHex},
                          trust::TrustedKey{"new-key", fixtures::kTestPublicKeyHex}};
    const EcdsaP256Verifier verifier(keys);
    CHECK_EQ(verifier.TrustedKeyCount(), 2u);
    auto verdict = verifier.VerifySignatureFile(fixtures::kStable_1_2_0.manifest, fixtures::kStable_1_2_0.signature);
    REQUIRE(verdict.ok());
    CHECK_EQ(*verdict, std::string("new-key"));
}

AETHER_TEST("ecdsa: unusable trusted keys are left out and an empty verifier trusts nothing") {
    const std::string offCurve(128, '1');  // X = Y = 0x1111...: not a point on P-256
    const std::array keys{trust::TrustedKey{"not-hex", "zz"}, trust::TrustedKey{"short", "0102"},
                          trust::TrustedKey{"off-curve", offCurve}};
    const EcdsaP256Verifier verifier(keys);
    CHECK_EQ(verifier.TrustedKeyCount(), 0u);
    auto verdict = verifier.VerifySignatureFile(fixtures::kStable_1_2_0.manifest, fixtures::kStable_1_2_0.signature);
    REQUIRE_FALSE(verdict.ok());
    CHECK_EQ(verdict.error().code, ErrorCode::Signature);

    // A bad key next to a good one does not disable the good one (but cannot add trust either).
    const std::array mixed{trust::TrustedKey{"off-curve", offCurve},
                           trust::TrustedKey{"test-key", fixtures::kTestPublicKeyHex}};
    CHECK_EQ(EcdsaP256Verifier(mixed).TrustedKeyCount(), 1u);
}

AETHER_TEST("ecdsa: signature files must be strict base64 of P1363 or DER") {
    const auto& fixture = fixtures::kStable_1_2_0;
    const std::string sig(fixture.signature);
    const std::string body = sig.substr(0, sig.size() - 1);
    for (const std::string& bad : {std::string(), std::string("\n"), std::string("not base64!"),
                                   body + "\n\n", body + " ", " " + body, body.substr(0, 20) + "\n" + body.substr(20),
                                   body.substr(0, body.size() - 4) + "\n", std::string(600, 'A'),
                                   "-----BEGIN SIGNATURE-----\n" + sig}) {
        auto parsed = ParseSignatureFile(bad);
        CHECK_FALSE(parsed.ok());
        if (!parsed) CHECK_EQ(parsed.error().code, ErrorCode::Signature);
    }
    auto good = ParseSignatureFile(sig);
    CHECK(good.ok());
    CHECK(ParseSignatureFile(body).ok());  // the trailing newline is optional
}

AETHER_TEST("ecdsa: the DER parser accepts only minimal, in-range encodings") {
    const auto der = FixtureDer(fixtures::kStable_1_2_0);
    REQUIRE_EQ(der[0], 0x30);
    auto parsed = ParseDerEcdsaSignature(der);
    REQUIRE(parsed.ok());
    // The converted r || s verifies like the P1363 signature written by sign_manifest.py.
    CHECK(TestVerifier().Verify(fixtures::kStable_1_2_0.manifest, *parsed).ok());

    auto expectInvalid = [](const std::vector<std::uint8_t>& bytes) {
        auto result = ParseDerEcdsaSignature(bytes);
        CHECK_FALSE(result.ok());
        if (!result) CHECK_EQ(result.error().code, ErrorCode::Signature);
    };

    // Build canonical encodings from scratch to mutate them precisely.
    const std::vector<std::uint8_t> one{0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01};
    CHECK(ParseDerEcdsaSignature(one).ok());  // r = s = 1 is structurally valid

    expectInvalid({0x31, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01});              // not a SEQUENCE
    expectInvalid({0x30, 0x81, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01});        // long-form length
    expectInvalid({0x30, 0x07, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01});              // length mismatch
    expectInvalid({0x30, 0x06, 0x03, 0x01, 0x01, 0x02, 0x01, 0x01});              // not an INTEGER
    expectInvalid({0x30, 0x07, 0x02, 0x02, 0x00, 0x01, 0x02, 0x01, 0x01});        // non-minimal (leading 00)
    expectInvalid({0x30, 0x06, 0x02, 0x01, 0x81, 0x02, 0x01, 0x01});              // negative
    expectInvalid({0x30, 0x06, 0x02, 0x01, 0x00, 0x02, 0x01, 0x01});              // r = 0
    expectInvalid({0x30, 0x06, 0x02, 0x00, 0x02, 0x02, 0x01, 0x01});              // empty INTEGER
    expectInvalid({0x30, 0x08, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01, 0x05, 0x00});  // trailing data in SEQUENCE
    {
        auto trailing = der;
        trailing.push_back(0x00);  // trailing byte after the SEQUENCE
        expectInvalid(trailing);
    }
    {
        // s = n (the group order) is out of range.
        std::vector<std::uint8_t> order{0x30, 0x26, 0x02, 0x01, 0x01, 0x02, 0x21, 0x00,
                                        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
                                        0xFF, 0xFF, 0xFF, 0xFF, 0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17, 0x9E, 0x84,
                                        0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x51};
        expectInvalid(order);
        order.back() = 0x50;  // n - 1 is the largest valid value
        CHECK(ParseDerEcdsaSignature(order).ok());
    }
    {
        // A 33-byte INTEGER without the sign-padding role (first byte non-zero) is too large.
        std::vector<std::uint8_t> big{0x30, 0x26, 0x02, 0x01, 0x01, 0x02, 0x21, 0x01};
        big.resize(big.size() + 32, 0x01);
        expectInvalid(big);
    }
    expectInvalid({});
    expectInvalid(std::vector<std::uint8_t>(73, 0x30));  // longer than any P-256 DER signature

    // P1363 with r = 0 or s >= n is rejected, as are other lengths that are not DER.
    CHECK_FALSE(ParseEcdsaSignature(std::vector<std::uint8_t>(64, 0x00)).ok());
    CHECK_FALSE(ParseEcdsaSignature(std::vector<std::uint8_t>(64, 0xFF)).ok());
    CHECK_FALSE(ParseEcdsaSignature(std::vector<std::uint8_t>(63, 0x01)).ok());
    CHECK_FALSE(ParseEcdsaSignature(std::vector<std::uint8_t>(65, 0x01)).ok());
    CHECK(ParseEcdsaSignature(std::vector<std::uint8_t>(64, 0x01)).ok());
}

AETHER_TEST("ecdsa: verification is safe to run concurrently") {
    const auto verifier = TestVerifier();
    std::vector<std::thread> threads;
    std::array<bool, 4> results{};
    for (std::size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&, i] {
            bool ok = true;
            for (int round = 0; round < 10; ++round) {
                ok = ok && verifier.VerifySignatureFile(fixtures::kStable_1_2_0.manifest,
                                                        fixtures::kStable_1_2_0.signature)
                               .ok();
            }
            results[i] = ok;
        });
    }
    for (auto& thread : threads) thread.join();
    for (const bool ok : results) CHECK(ok);
}
