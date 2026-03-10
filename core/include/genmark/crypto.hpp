#pragma once

#include "types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace genmark {

// ─────────────────────────────────────────────────────────────────────────────
// Cryptographic subsystem — backed by libsodium (statically linked)
// ─────────────────────────────────────────────────────────────────────────────
//
// Primitives used (§2.1 of engineering spec):
//
//   SHA-256       crypto_hash_sha256()            — content hash, msg assembly
//   HKDF-SHA256   crypto_kdf_derive_from_key()    — content-bound nonce
//   Ed25519       crypto_sign_detached()           — signing
//   Ed25519       crypto_sign_verify_detached()    — verification
//
// Threading
// ─────────
// crypto_init() must be called once from the main thread before any other
// function in this file.  After that every function is stateless and safe to
// call concurrently from multiple threads — libsodium provides this guarantee
// after sodium_init() returns 0 or 1 (already-initialised).
//
// Memory hygiene
// ──────────────
// SecretKey holds 64 bytes of Ed25519 secret key material.  The type is
// move-only; its destructor wipes the buffer with sodium_memzero() so that
// key bytes are not readable in freed heap pages.  Never store a SecretKey in
// a container that copies or shares ownership.
//
// Error handling
// ──────────────
// Every fallible function returns Status.  All return values are [[nodiscard]].
// There are no exceptions and no silent failures.

// ─── Constants ────────────────────────────────────────────────────────────────
//
// Hard-coded from libsodium's documented ABI — stable across all libsodium
// versions >= 1.0.0.  Static assertions in crypto.cpp verify these at build time
// against the actual library values so a version mismatch is a compile error,
// not a silent runtime bug.

inline constexpr std::size_t kPublicKeyBytes  = 32; // Ed25519 public key
inline constexpr std::size_t kSecretKeyBytes  = 64; // Ed25519 secret key (seed || public)
inline constexpr std::size_t kSignatureBytes  = 64; // Ed25519 signature
inline constexpr std::size_t kSeedBytes       = 32; // Ed25519 seed (private scalar)
inline constexpr std::size_t kDigestBytes     = 32; // SHA-256 output
inline constexpr std::size_t kNonceBytes      = 32; // HKDF-derived content nonce

// Payload wire-format minimum size (§6.1) — 133 bytes with zero-length metadata:
//   1  (version)
//   32 (fingerprint F)
//   32 (nonce n)
//   4  (metadata_len u32 LE)
//   64 (signature S)
inline constexpr std::size_t kFingerprintBytes = kDigestBytes;
inline constexpr std::size_t kPayloadMinBytes  =
    1 + kFingerprintBytes + kNonceBytes + 4 + kSignatureBytes;

// ─── Type aliases ─────────────────────────────────────────────────────────────

using Digest    = std::array<std::uint8_t, kDigestBytes>;
using Nonce     = std::array<std::uint8_t, kNonceBytes>;
using Signature = std::array<std::uint8_t, kSignatureBytes>;
using PublicKey = std::array<std::uint8_t, kPublicKeyBytes>;

// ─── SecretKey ────────────────────────────────────────────────────────────────
//
// RAII wrapper around 64 bytes of Ed25519 secret key material.
// Move-only.  Destructor wipes the buffer with sodium_memzero() before
// deallocation so key bytes never linger in freed heap pages.
//
// Internal layout (libsodium convention):
//   bytes[0..31]  — seed / private scalar
//   bytes[32..63] — public key (copy, for fast detached signing)

class SecretKey {
public:
    // Construct zeroed — not usable for signing until populated via
    // make_keypair() or keypair_from_seed().
    SecretKey() noexcept;
    ~SecretKey() noexcept; // sodium_memzero

    // Move-only: copying secret key bytes is prohibited.
    SecretKey(const SecretKey&)            = delete;
    SecretKey& operator=(const SecretKey&) = delete;
    SecretKey(SecretKey&&)                 noexcept;
    SecretKey& operator=(SecretKey&&)      noexcept;

    [[nodiscard]] const std::uint8_t* data() const noexcept;
    [[nodiscard]]       std::uint8_t* data()       noexcept;

    // Returns the public key embedded in bytes[32..63].
    [[nodiscard]] PublicKey public_key() const noexcept;

private:
    std::uint8_t bytes_[kSecretKeyBytes];
};

// ─── Library initialisation ───────────────────────────────────────────────────
//
// Wraps sodium_init().  Must be called once before any other function in this
// file, from the main thread, before spawning any threads that use crypto.
// Subsequent calls are safe no-ops (sodium_init is idempotent).
//
// Returns Status::InvalidState if sodium_init() returns -1 (system CSPRNG
// unavailable — unrecoverable).

[[nodiscard]] Status crypto_init() noexcept;

// ─── Key generation ───────────────────────────────────────────────────────────
//
// Generates a fresh Ed25519 keypair from the system CSPRNG.
// crypto_init() must have been called before this.

[[nodiscard]] Status make_keypair(SecretKey& sk, PublicKey& vk) noexcept;

// Derives a deterministic keypair from a 32-byte seed.
// Same seed always produces the same keypair — useful for testing and for
// key derivation schemes.  In production, seed must come from a CSPRNG.

[[nodiscard]] Status keypair_from_seed(
    std::span<const std::uint8_t, kSeedBytes> seed,
    SecretKey& sk,
    PublicKey& vk) noexcept;

// ─── SHA-256 ──────────────────────────────────────────────────────────────────
//
// Single-call SHA-256.  Used for content fingerprint hashing and for the
// metadata hash inside assemble_message().
// Both overloads are provided — the std::byte variant matches FFmpeg buffer
// conventions; the uint8_t variant matches libsodium conventions.

[[nodiscard]] Digest sha256(std::span<const std::uint8_t> data) noexcept;
[[nodiscard]] Digest sha256(std::span<const std::byte>    data) noexcept;

// ─── HKDF nonce derivation ────────────────────────────────────────────────────
//
// Derives the 32-byte content-bound nonce from the canonical fingerprint F.
//
// Construction (§2.3):
//   n = crypto_kdf_derive_from_key(
//           out_len  = 32,
//           subkey_id = 1,
//           context  = "genmark-v",   // exactly 8 bytes, no null terminator
//           key      = F)             // 32-byte fingerprint as KDF master key
//
// IMPORTANT: the fingerprint is used as the KDF *key*, not as input data.
// This is what makes the nonce content-bound — the nonce changes if and only
// if the fingerprint changes.  Both creator and verifier derive it independently
// from their locally-computed fingerprint; no nonce is transmitted.
//
// Wait — the nonce IS serialised into the payload (§6.1 wire format).  It is
// included there so the verifier can detect bit-corruption in the nonce field
// itself, but the authoritative nonce for verification is always recomputed
// from the live fingerprint, never taken from the payload directly.

[[nodiscard]] Nonce derive_nonce(
    std::span<const std::uint8_t, kFingerprintBytes> fingerprint) noexcept;

// ─── Signed message assembly ──────────────────────────────────────────────────
//
// Builds the 96-byte buffer that Ed25519 operates on.
//
// Layout (§2.3):
//   [0..31]  = F                   canonical fingerprint
//   [32..63] = n                   HKDF nonce (derived from F)
//   [64..95] = SHA256(metadata)    hash of raw metadata bytes
//
// The metadata is hashed rather than appended so the signed buffer is always
// exactly 96 bytes regardless of metadata length.  metadata may be empty.
//
// Both signing and verification must call this function with their respective
// locally-derived F and n — never with values taken from the payload.

using SignedMessage = std::array<std::uint8_t, 96>;

[[nodiscard]] SignedMessage assemble_message(
    std::span<const std::uint8_t, kFingerprintBytes> fingerprint,
    const Nonce&                                      nonce,
    std::span<const std::uint8_t>                     metadata) noexcept;

// ─── Signing ──────────────────────────────────────────────────────────────────
//
// Produces a 64-byte detached Ed25519 signature over msg using sk.
//
// Full signing sequence:
//   F   = canonical_fingerprint(media)
//   n   = derive_nonce(F)
//   msg = assemble_message(F, n, metadata)
//   sig = sign_message(msg, sk)          ← this function
//
// The signature is deterministic for a given (msg, sk) pair — Ed25519 uses
// a hash-derived per-message scalar r (RFC 8032 §5.1.6), not a random nonce.
// No state is required between calls.

[[nodiscard]] Signature sign_message(
    const SignedMessage& msg,
    const SecretKey&     sk) noexcept;

// ─── Verification ─────────────────────────────────────────────────────────────
//
// Verifies sig over msg against public key vk.
//
// Returns Status::Ok         — signature is valid.
// Returns Status::AuthFailed — signature is invalid (wrong key, forged, or
//                              corrupt).  Treat as an authentication failure,
//                              not a programming error.
//
// Full verification sequence (§1.2):
//   P                    = extract_watermark(candidate_media)
//   (F_emb, meta, sig)   = deserialise(P)
//   F_live               = canonical_fingerprint(candidate_media)
//   n_live               = derive_nonce(F_live)
//   msg_live             = assemble_message(F_live, n_live, meta)
//   s1 = verify_message(msg_live, sig, vk)  ← this function
//   s2 = fingerprints_equal(F_emb, F_live)
//   authenticated = (s1 == Ok) && (s2 == Ok)
//
// Checking BOTH s1 and s2 is mandatory.  Checking only the signature allows
// a transplant attack (§7.3).  Checking only the fingerprint allows a forged
// payload with a garbage signature to pass if the fingerprint happens to match.

[[nodiscard]] Status verify_message(
    const SignedMessage& msg,
    const Signature&     sig,
    const PublicKey&     vk) noexcept;

// ─── Constant-time fingerprint comparison ────────────────────────────────────
//
// Compares two digests in constant time using sodium_memcmp().
//
// MUST be used instead of memcmp() or operator== for all comparisons of
// values derived from secret material.  Timing side-channels on comparison
// of fingerprints (which are derived from content that an attacker may be
// probing) are a realistic attack surface.
//
// Returns Status::Ok         — digests are identical.
// Returns Status::AuthFailed — digests differ.

[[nodiscard]] Status fingerprints_equal(
    const Digest& a,
    const Digest& b) noexcept;

} // namespace genmark