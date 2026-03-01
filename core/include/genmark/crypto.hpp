#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace genmark::crypto {

    using Hash = std::array<uint8_t, 32>;
    using Signature = std::array<uint8_t, 64>;
    using PublicKey = std::array<uint8_t, 32>;
    using PrivateKey = std::array<uint8_t, 64>;

    bool init();  // wraps sodium_init()

    Hash sha256(const uint8_t* data, size_t len);

    Signature sign(const uint8_t* msg,
                   size_t len,
                   const PrivateKey& sk);

    bool verify(const uint8_t* msg,
                size_t len,
                const Signature& sig,
                const PublicKey& pk);

}