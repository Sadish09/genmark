#pragma once

#include "types.hpp"
#include <span>

namespace genmark {

    //Signal domain stuff - may include FFT later on for audio tagging too
    class ITransform {
    public:
        virtual ~ITransform() = default;

        virtual Status forward(std::span<Byte> media) = 0;
        virtual Status inverse(std::span<Byte> media) = 0;

        virtual MediaType type() const noexcept = 0;
    };

} // namespace genmark