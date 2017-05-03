#pragma once

#include "cocaine/vicodyn/peer.hpp"

#include <cocaine/format.hpp>
#include <cocaine/format/base.hpp>

#include <iomanip>

namespace cocaine {

///TODO: move to core
template<class Clock, class Duration>
struct display<std::chrono::time_point<Clock, Duration>> {
    using value_t = std::chrono::time_point<Clock, Duration>;
    static
    auto
    apply(std::ostream& stream, const value_t& value) -> std::ostream& {
        std::time_t time = Clock::to_time_t(value);
        stream << std::put_time(std::localtime(&time), "%F %T");
        return stream;
    }
};

template<>
struct display<vicodyn::peer_t> {
    static
    auto
    apply(std::ostream& stream, const vicodyn::peer_t& value) -> std::ostream& {
        stream << format("{\"state\": \"{}\", "
                          "\"uuid\": \"{}\", "
                          "\"local\": \"{}\", "
                          "\"freezed_till\": \"{}\", "
                          "\"last_used\": \"{}\"}",
                         value.state(), value.uuid(), value.local(), value.freezed_till(), value.last_used());
        return stream;
    }
};

template<>
struct display<vicodyn::peer_t::state_t> {
    static
    auto
    apply(std::ostream& stream, vicodyn::peer_t::state_t value) -> std::ostream& {
        switch (value) {
            case vicodyn::peer_t::state_t::disconnected:
                stream << "disconnected";
                break;
            case vicodyn::peer_t::state_t::connecting:
                stream << "connecting";
                break;
            case vicodyn::peer_t::state_t::connected:
                stream << "connected";
                break;
            case vicodyn::peer_t::state_t::freezed:
                stream << "freezed";
                break;
        }
        return stream;
    }
};

template<>
struct display_traits<vicodyn::peer_t>: public lazy_display<vicodyn::peer_t> {};

template<>
struct display_traits<vicodyn::peer_t::state_t>: public lazy_display<vicodyn::peer_t::state_t> {};


template<class Clock, class Duration>
struct display_traits<std::chrono::time_point<Clock, Duration>>:
        public lazy_display<std::chrono::time_point<Clock, Duration>>
{};

} // namespace cocaine
