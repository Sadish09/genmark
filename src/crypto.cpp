#include "genmark/crypto.hpp"

#include <cstdint>
#include <sodium.h>
#include <stdexcept>

namespace genmark::crypto {

    bool init()
    {
        return sodium_init() >= 0;
    }

    Hash sha256(const uint8_t* data, size_t len)
    {
        Hash hash;

        crypto_hash_sha256(
            hash.data(),
            data,
            len
        );

        return hash;
    }

    Signature sign(const uint8_t* msg,
                   size_t len,
                   const PrivateKey& sk)
    {
        Signature sig;

        crypto_sign_detached(
            sig.data(),
            nullptr,
            msg,
            len,
            sk.data()
        );

        return sig;
    }

    bool verify(const uint8_t* msg,
                size_t len,
                const Signature& sig,
                const PublicKey& pk)
    {
        return crypto_sign_verify_detached(
            sig.data(),
            msg,
            len,
            pk.data()
        ) == 0;
    }

}