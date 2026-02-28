#pragma once

#include "types.hpp"
#include "transform.hpp"
#include <span>
#include <vector>

namespace genmark {


    class IWatermarkStrategy {
    public:
        virtual ~IWatermarkStrategy() = default;

        virtual std::size_t capacity_bytes() const = 0;

        virtual Status embed(ITransform& transform,
                             std::span<Byte> media,
                             std::span<const Byte> payload) = 0;

        virtual Status extract(ITransform& transform,
                               std::span<Byte> media,
                               std::vector<Byte>& out_payload) = 0;
    };

} // namespace genmark