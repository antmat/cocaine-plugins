#pragma once

#include "cocaine/vicodyn/peer.hpp"

#include <cocaine/format.hpp>
#include <cocaine/format/base.hpp>

namespace cocaine {

template<>
struct display<vicodyn::peer_t> {
    static
    auto
    apply(std::ostream& stream, const vicodyn::peer_t& value) -> std::ostream& {
        stream << "{endpoints: " << value.endpoints;
        stream << ", queue_connected: " << value.queue->connected();
        stream << "}";
    }
};

template<>
struct display_traits<vicodyn::peer_t>: public lazy_display<vicodyn::peer_t> {};

} // namespace cocaine
