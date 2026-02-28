#pragma once

#include "types.hpp"
#include "payload.hpp"
#include "watermark.hpp"
#include <memory>
#include <span>

namespace genmark {

    //depends on signing provider, can swap monocypher with anything of your choice
    class SigningProvider {
    public:
        Hash sha256(std::span<const Byte> data);
        Signature sign(const Hash& hash);
    };

    //Server side only, separate wasm file will be provided
    class Creator {
    public:
        Creator(SigningProvider signer,
                std::unique_ptr<ITransform> transform,
                std::unique_ptr<IWatermarkStrategy> strategy)
            : signer_(std::move(signer)),
              transform_(std::move(transform)),
              strategy_(std::move(strategy)) {}

        Status create(std::span<Byte> media,
                      const Metadata& metadata)
        {
            Hash hash = signer_.sha256(media);
            Signature sig = signer_.sign(hash);

            Payload payload{ hash, metadata, sig };

            auto serialized = codec_.serialize(payload);

            if (serialized.size() > strategy_->capacity_bytes())
                return Status::PayloadTooLarge;

            return strategy_->embed(
                *transform_, media, serialized
            );
        }

    private:
        SigningProvider signer_;
        PayloadCodec codec_;
        std::unique_ptr<ITransform> transform_;
        std::unique_ptr<IWatermarkStrategy> strategy_;
    };

} // namespace genmark