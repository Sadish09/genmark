#pragma once

#include "types.hpp"
#include <vector>

namespace genmark {

    struct Payload {
        Hash      content_hash;
        Metadata  metadata;
        Signature signature;
    };

    /*
      Responsible for:
       Binary serialization
       Version info
       Backward compatibility later
     */
    class PayloadCodec {
    public:
        std::vector<Byte> serialize(const Payload& payload);

        Status deserialize(std::span<const Byte> data,
                           Payload& out_payload);
    };

} // namespace genmark