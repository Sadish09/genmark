#pragma once

#include "types.hpp"
#include "payload.hpp"
#include "watermark.hpp"
#include <memory>
#include <span>

namespace genmark {


    class VerificationProvider {
    public:
        Hash sha256(std::span<const Byte> data);

        bool verify(const Hash& hash,
                    const Signature& sig,
                    const PublicKey& pubkey);
    };

    //Verifier has wasm and py bindings
    class Verifier {
    public:
        Verifier(VerificationProvider verifier,
                 PublicKey public_key,
                 std::unique_ptr<ITransform> transform,
                 std::unique_ptr<IWatermarkStrategy> strategy)
            : verifier_(std::move(verifier)),
              public_key_(public_key),
              transform_(std::move(transform)),
              strategy_(std::move(strategy)) {}

        Status verify(std::span<Byte> media)
        {
            std::vector<Byte> serialized;

            Status s = strategy_->extract(
                *transform_, media, serialized
            );
            if (s != Status::Ok)
                return s;

            Payload payload;

            s = codec_.deserialize(serialized, payload);
            if (s != Status::Ok)
                return s;

            Hash computed = verifier_.sha256(media);

            if (computed != payload.content_hash)
                return Status::CorruptedPayload;

            bool valid = verifier_.verify(
                payload.content_hash,
                payload.signature,
                public_key_
            );

            return valid ? Status::Ok
                         : Status::InvalidSignature;
        }

    private:
        VerificationProvider verifier_;
        PublicKey public_key_;
        PayloadCodec codec_;
        std::unique_ptr<ITransform> transform_;
        std::unique_ptr<IWatermarkStrategy> strategy_;
    };

} // namespace genmark