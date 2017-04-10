#pragma once

#include "cocaine/api/peer/pool.hpp"

#include "cocaine/vicodyn/peer.hpp"

#include <cocaine/locked_ptr.hpp>

namespace cocaine {
namespace vicodyn {
namespace peer {
namespace pool {

class basic_t : public api::peer::pool_t
{
public:


    typedef std::map<std::string, remote_t> peers_t;

    basic_t(context_t& _context, asio::io_service& _io_loop, const std::string& service_name, const dynamic_t&);

    ~basic_t();

    auto invoke(const io::aux::decoded_message_t& incoming_message,
                const io::graph_node_t& protocol,
                stream_ptr_t backward_stream) -> stream_ptr_t override;


    auto register_real(std::string uuid, std::vector<asio::ip::tcp::endpoint> ep, bool) -> void override;

    auto deregister_real(const std::string& uuid) -> void override;

    auto size() -> size_t override;

private:
    auto choose_peer() -> std::pair<std::string, std::shared_ptr<peer_t>>;
    auto connect_peer(std::shared_ptr<peer_t> peer, peers_t& remote_map) -> void;
    auto rebalance_peers() -> void;
    auto on_peer_error(const std::string& uuid, std::future<void> future) -> void;

    std::shared_ptr<dispatch<io::context_tag>> signal_dispatcher;

    size_t pool_size;
    size_t retry_count;
    std::chrono::milliseconds freeze_time;
    std::chrono::milliseconds reconnect_age;
    context_t& context;
    std::unique_ptr<logging::logger_t> logger;
    asio::io_service& io_loop;
    asio::deadline_timer rebalance_timer;

    synchronized<peers_t> remotes;
};

} // namespace pool
} // namespace peer
} // namespace vicodyn
} // namespace cocaine
