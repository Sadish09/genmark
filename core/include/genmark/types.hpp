#pragma once

#include <cstdint>
#include <array>
#include <span> //span because python compliance
#include <string>

namespace genmark {


    constexpr std::size_t HASH_SIZE       = 32; // SHA-256
    constexpr std::size_t SIGNATURE_SIZE  = 64; // Ed25519
    constexpr std::size_t PUBLIC_KEY_SIZE = 32;


    using Byte = std::uint8_t;

    using Hash      = std::array<Byte, HASH_SIZE>;
    using Signature = std::array<Byte, SIGNATURE_SIZE>;
    using PublicKey = std::array<Byte, PUBLIC_KEY_SIZE>;


    struct Metadata {
        std::string  creator_id;
        std::uint64_t timestamp;
        std::uint32_t version;
    };

    enum class MediaType : std::uint8_t {
        Image,
        Audio,
        Video,
        Unknown
    };

    enum class Status : std::uint8_t {
        Ok = 0,
        InvalidSignature,
        WatermarkNotFound,
        TransformError,
        CorruptedPayload,
        PayloadTooLarge,
        UnsupportedFormat
    };

} // namespace genmark