#include "genmark/core/include/genmark/crypto.hpp"

// libsodium — statically linked in release builds, shared in development.
// The extern "C" wrapper is not needed; sodium.h handles this itself.
#include <sodium.h>

#include <algorithm>
#include <cassert>
#include <cstring>

// ─── Build-time ABI verification ─────────────────────────────────────────────
//
// If any of these fire, the libsodium version on this machine does not match
// the constants declared in crypto.hpp.  Update the constants or the library.
//
// We use the function-call forms (e.g. crypto_sign_ed25519_publickeybytes())
// rather than the macro forms because the macros are not always defined in
// older headers, but the functions are always present in the .so.
//
// These are static_asserts on constexpr values so they are zero-cost.

static_assert(genmark::kPublicKeyBytes == crypto_sign_ed25519_PUBLICKEYBYTES,
    "kPublicKeyBytes mismatch with libsodium");
static_assert(genmark::kSecretKeyBytes == crypto_sign_ed25519_SECRETKEYBYTES,
    "kSecretKeyBytes mismatch with libsodium");
static_assert(genmark::kSignatureBytes == crypto_sign_ed25519_BYTES,
    "kSignatureBytes mismatch with libsodium");
static_assert(genmark::kSeedBytes      == crypto_sign_ed25519_SEEDBYTES,
    "kSeedBytes mismatch with libsodium");
static_assert(genmark::kDigestBytes    == crypto_hash_sha256_BYTES,
    "kDigestBytes mismatch with libsodium");
static_assert(genmark::kNonceBytes     == crypto_kdf_KEYBYTES,
    "kNonceBytes mismatch: nonce is derived via crypto_kdf whose output size "
    "equals crypto_kdf_KEYBYTES; ensure kNonceBytes == 32");

// crypto_kdf context must be exactly 8 bytes — verified at the call site.
static_assert(crypto_kdf_CONTEXTBYTES == 8,
    "crypto_kdf_CONTEXTBYTES is not 8; the context literal below is wrong");

namespace genmark {

// ─────────────────────────────────────────────────────────────────────────────
// SecretKey
// ─────────────────────────────────────────────────────────────────────────────

SecretKey::SecretKey() noexcept {
    sodium_memzero(bytes_, sizeof(bytes_));
}

SecretKey::~SecretKey() noexcept {
    sodium_memzero(bytes_, sizeof(bytes_));
}

SecretKey::SecretKey(SecretKey&& other) noexcept {
    std::memcpy(bytes_, other.bytes_, sizeof(bytes_));
    sodium_memzero(other.bytes_, sizeof(other.bytes_));
}

SecretKey& SecretKey::operator=(SecretKey&& other) noexcept {
    if (this != &other) {
        sodium_memzero(bytes_, sizeof(bytes_));
        std::memcpy(bytes_, other.bytes_, sizeof(bytes_));
        sodium_memzero(other.bytes_, sizeof(other.bytes_));
    }
    return *this;
}

const std::uint8_t* SecretKey::data() const noexcept { return bytes_; }
      std::uint8_t* SecretKey::data()       noexcept { return bytes_; }

PublicKey SecretKey::public_key() const noexcept {
    // libsodium stores the public key in bytes[32..63] of the extended secret
    // key.  This is documented behaviour in the Ed25519 API.
    PublicKey vk;
    std::memcpy(vk.data(), bytes_ + kSeedBytes, kPublicKeyBytes);
    return vk;
}

// ─────────────────────────────────────────────────────────────────────────────
// Library initialisation
// ─────────────────────────────────────────────────────────────────────────────

Status crypto_init() noexcept {
    // sodium_init() returns:
    //   0  — initialised successfully
    //   1  — already initialised (safe, treat as success)
    //  -1  — initialisation failed (system CSPRNG unavailable)
    const int rc = sodium_init();
    return (rc >= 0) ? Status::Ok : Status::InvalidState;
}

// ─────────────────────────────────────────────────────────────────────────────
// Key generation
// ─────────────────────────────────────────────────────────────────────────────

Status make_keypair(SecretKey& sk, PublicKey& vk) noexcept {
    // crypto_sign_keypair writes:
    //   pk (32 bytes) into vk.data()
    //   sk (64 bytes, seed || pk) into sk.data()
    const int rc = crypto_sign_keypair(vk.data(), sk.data());
    // libsodium documents this as always returning 0, but check anyway.
    return (rc == 0) ? Status::Ok : Status::InvalidState;
}

Status keypair_from_seed(
    std::span<const std::uint8_t, kSeedBytes> seed,
    SecretKey& sk,
    PublicKey& vk) noexcept
{
    const int rc = crypto_sign_ed25519_seed_keypair(vk.data(), sk.data(), seed.data());
    return (rc == 0) ? Status::Ok : Status::InvalidArgument;
}

// ─────────────────────────────────────────────────────────────────────────────
// SHA-256
// ─────────────────────────────────────────────────────────────────────────────

Digest sha256(std::span<const std::uint8_t> data) noexcept {
    Digest out;
    crypto_hash_sha256(out.data(), data.data(), data.size());
    return out;
}

Digest sha256(std::span<const std::byte> data) noexcept {
    // std::byte and uint8_t are both 1-byte types; reinterpret_cast is safe
    // here and is the standard practice for bridging FFmpeg/STL byte buffers
    // to libsodium's uint8_t* API.
    return sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(data.data()),
        data.size()));
}

// ─────────────────────────────────────────────────────────────────────────────
// HKDF nonce derivation
// ─────────────────────────────────────────────────────────────────────────────

Nonce derive_nonce(
    std::span<const std::uint8_t, kFingerprintBytes> fingerprint) noexcept
{
    // crypto_kdf_derive_from_key parameters:
    //
    //   subkey     — output buffer
    //   subkeylen  — must be between crypto_kdf_BYTES_MIN and crypto_kdf_BYTES_MAX
    //                (libsodium 1.0.12+: both are 16..64 for the default BLAKE2b
    //                backend, so 32 is always valid)
    //   subkey_id  — domain-separation integer; we use 1
    //   ctx        — exactly 8-byte context string (no null terminator counted)
    //   key        — 32-byte master key; we use the fingerprint F
    //
    // Context string "genmark-v" is 9 chars — we need exactly 8 bytes.
    // Use "genmarkv" (8 bytes, no hyphen) to fit the API exactly.
    // This value is fixed in the spec and must never change; changing it
    // invalidates all previously issued signatures.

    static constexpr char kContext[crypto_kdf_CONTEXTBYTES] = {
        'g','e','n','m','a','r','k','v'
    };

    Nonce nonce;
    crypto_kdf_derive_from_key(
        nonce.data(),
        nonce.size(),
        1,           // subkey_id
        kContext,
        fingerprint.data());
    return nonce;
}

// ─────────────────────────────────────────────────────────────────────────────
// Signed message assembly
// ─────────────────────────────────────────────────────────────────────────────

SignedMessage assemble_message(
    std::span<const std::uint8_t, kFingerprintBytes> fingerprint,
    const Nonce&                                      nonce,
    std::span<const std::uint8_t>                     metadata) noexcept
{
    // Layout: [F(32)] [n(32)] [SHA256(meta)(32)] = 96 bytes total
    SignedMessage msg;

    std::memcpy(msg.data(),      fingerprint.data(), kFingerprintBytes);
    std::memcpy(msg.data() + 32, nonce.data(),       kNonceBytes);

    // Hash the metadata into the final 32 bytes.
    // SHA256 of an empty span is well-defined (SHA256("") = known constant).
    crypto_hash_sha256(
        msg.data() + 64,
        metadata.data(),
        metadata.size());

    return msg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Signing
// ─────────────────────────────────────────────────────────────────────────────

Signature sign_message(
    const SignedMessage& msg,
    const SecretKey&     sk) noexcept
{
    Signature sig;

    // crypto_sign_detached:
    //   sig      — output, exactly 64 bytes
    //   siglen   — optional output for actual sig length; always 64 for Ed25519
    //   msg      — message to sign
    //   msglen   — message length in bytes
    //   sk       — 64-byte extended secret key (seed || pk)
    //
    // Returns 0 always for Ed25519 (documented).
    crypto_sign_detached(
        sig.data(),
        nullptr,       // siglen_p — we know it's always kSignatureBytes
        msg.data(),
        msg.size(),
        sk.data());

    return sig;
}

// ─────────────────────────────────────────────────────────────────────────────
// Verification
// ─────────────────────────────────────────────────────────────────────────────

Status verify_message(
    const SignedMessage& msg,
    const Signature&     sig,
    const PublicKey&     vk) noexcept
{
    // crypto_sign_verify_detached:
    //   Returns  0 — valid
    //   Returns -1 — invalid (forged, corrupt, wrong key)
    const int rc = crypto_sign_verify_detached(
        sig.data(),
        msg.data(),
        msg.size(),
        vk.data());

    return (rc == 0) ? Status::Ok : Status::AuthFailed;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constant-time fingerprint comparison
// ─────────────────────────────────────────────────────────────────────────────

Status fingerprints_equal(const Digest& a, const Digest& b) noexcept {
    // sodium_memcmp is guaranteed constant-time regardless of content.
    // Returns 0 if equal, -1 if not equal (intentionally not 1 to prevent
    // misuse as a direct boolean — callers must compare against 0).
    const int rc = sodium_memcmp(a.data(), b.data(), kDigestBytes);
    return (rc == 0) ? Status::Ok : Status::AuthFailed;
}

} // namespace genmark