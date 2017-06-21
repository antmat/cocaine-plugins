/*
* 2015+ Copyright (c) Anton Matveenko <antmat@yandex-team.ru>
* All rights reserved.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*/

#include "cocaine/cluster/unicorn.hpp"

#include "cocaine/dynamic/endpoint.hpp"

#include <cocaine/context.hpp>
#include <cocaine/context/quote.hpp>
#include <cocaine/errors.hpp>
#include <cocaine/logging.hpp>
#include <cocaine/rpc/actor.hpp>
#include <cocaine/unicorn/value.hpp>

#include <blackhole/logger.hpp>

#include <boost/optional/optional.hpp>

#include <zookeeper/zookeeper.h>

namespace cocaine { namespace cluster {

namespace ph = std::placeholders;

typedef api::unicorn_t::response response;

unicorn_cluster_t::cfg_t::cfg_t(const dynamic_t& args) :
        path(args.as_object().at("path", "/cocaine/discovery").as_string()),
        retry_interval(args.as_object().at("retry_interval", 1u).as_uint()),
        check_interval(args.as_object().at("check_interval", 60u).as_uint())
{}

unicorn_cluster_t::timer_t::timer_t(unicorn_cluster_t& parent, std::function<void()> callback) :
    parent(parent),
    timer(parent.locator.asio()),
    callback(std::move(callback))
{}

auto unicorn_cluster_t::timer_t::defer(const boost::posix_time::time_duration& duration) -> void {
    timer.apply([&](asio::deadline_timer& timer){
        timer.expires_from_now(duration);
        timer.async_wait([&](const std::error_code& ec){
            if(!ec) {
                callback();
            }
        });
    });
}

auto unicorn_cluster_t::timer_t::defer_retry() -> void {
    defer(boost::posix_time::seconds(parent.config.retry_interval));
}

auto unicorn_cluster_t::timer_t::defer_check() -> void {
    defer(boost::posix_time::seconds(parent.config.check_interval));
}

unicorn_cluster_t::announcer_t::announcer_t(unicorn_cluster_t& parent) :
        parent(parent),
        timer(parent, [=](){announce();}),
        path(parent.config.path + '/' + parent.locator.uuid())
{}

auto unicorn_cluster_t::announcer_t::set_and_check_endpoints() -> bool {
    auto quote = parent.context.locate("locator");

    if(!quote) {
        return false;
    }

    if(endpoints.empty()) {
        endpoints = quote->endpoints;
    }

    if(quote->endpoints != endpoints) {
        // TODO: fix this case, if ever we would need it
        // This can happen only if actor endpoints have changed
        // which is not likely.
        BOOST_ASSERT_MSG(false, "endpoints changed for locator service, can not continue, terminating" );
        std::terminate();
    }
    return true;
}

auto unicorn_cluster_t::announcer_t::announce() -> void {
    if(!set_and_check_endpoints()) {
        COCAINE_LOG_WARNING(log, "unable to announce local endpoints: locator is not available");
        timer.defer_retry();
        return;
    }

    COCAINE_LOG_INFO(log, "going to announce self");
    try {
        auto callback = std::bind(&announcer_t::on_set, this, ph::_1);
        scope = parent.unicorn->create(std::move(callback), path, endpoints, true, false);
        timer.defer_check();
    } catch (const std::system_error& e) {
        COCAINE_LOG_WARNING(log, "could not announce self in unicorn - {}", error::to_string(e));
        timer.defer_retry();
    }
}

auto unicorn_cluster_t::announcer_t::on_check(std::future<response::subscribe> future) -> void {
    try {
        if (future.get().version() < 0) {
            COCAINE_LOG_ERROR(log, "announce disappeared, retrying");
            timer.defer_retry();
        }
    } catch (const std::system_error& e) {
        COCAINE_LOG_ERROR(log, "announce disappeared: {} , retrying", error::to_string(e));
        timer.defer_retry();
    }
}

auto unicorn_cluster_t::announcer_t::on_set(std::future<response::create> future) -> void {
    try {
        future.get();
        COCAINE_LOG_INFO(log, "announced self in unicorn");
        auto cb = std::bind(&announcer_t::on_check, this, ph::_1);
        scope = parent.unicorn->subscribe(std::move(cb), std::move(path));
    } catch (const std::system_error& e) {
        if(e.code() == error::node_exists) {
            COCAINE_LOG_INFO(log, "announce checked");
            return;
        }
        COCAINE_LOG_ERROR(log, "could not announce local services: {} ", error::to_string(e));
        timer.defer_retry();
    }
}

unicorn_cluster_t::subscriber_t::subscriber_t(unicorn_cluster_t& parent) :
        parent(parent),
        timer(parent, [=](){subscribe();})
{}


auto unicorn_cluster_t::subscriber_t::subscribe() -> void {
    auto cb = std::bind(&unicorn_cluster_t::subscriber_t::on_children, this, ph::_1);
    COCAINE_LOG_INFO(log, "subscribed for updates on path {} (may be prefixed)", config.path);
    timer.defer_check();
}

auto unicorn_cluster_t::subscriber_t::on_node(std::string uuid, std::future<response::subscribe> future) -> void {
    subscriptions.apply([&](subscriptions_t& subscriptions) {
        auto terminate = [&]() {
            parent.locator.drop_node(uuid);
            subscriptions.erase(uuid);
        };

        try {
            auto result = future.get();
            if(!result.exists()) {
                terminate();
            }
            auto endpoints = result.value().to<std::vector<asio::ip::tcp::endpoint>>();
            if(endpoints.empty()) {
                terminate();
            }
        } catch(const std::exception& e){
            COCAINE_LOG_INFO(parent.log, "node {} subscription failed - {}", uuid, e.what());
            terminate();
        }
    });
    try {
        drop_scope(scope_id);
        auto result = node_endpoints.get();
        COCAINE_LOG_INFO(log, "fetched {} node's endpoint from zookeeper", uuid);
        std::vector<asio::ip::tcp::endpoint> fetched_endpoints;
        if (result.value().convertible_to<std::vector<asio::ip::tcp::endpoint>>()) {
            fetched_endpoints = result.value().to<std::vector<asio::ip::tcp::endpoint>>();
        }
        if (fetched_endpoints.empty()) {
            COCAINE_LOG_WARNING(log, "invalid value for announce: {}",
                                boost::lexical_cast<std::string>(result.value()).c_str()
            );
            registered_locators.apply([&](locator_endpoints_t& endpoint_map) {
                endpoint_map.erase(uuid);
                locator.drop_node(uuid);
            });
        } else {
            registered_locators.apply([&](locator_endpoints_t& endpoint_map) {
                auto& cur_endpoints = endpoint_map[uuid];
                if (cur_endpoints.empty()) {
                    COCAINE_LOG_INFO(log, "linking {} node", uuid);
                } else {
                    COCAINE_LOG_INFO(log, "relinking {} node", uuid);
                }
                cur_endpoints = fetched_endpoints;
                locator.link_node(uuid, fetched_endpoints);
            });
        }
    } catch (const std::system_error& e) {
        COCAINE_LOG_WARNING(log, "error during fetch: {} ", error::to_string(e));
        registered_locators.apply([&](locator_endpoints_t& endpoint_map) {
            auto it = endpoint_map.find(uuid);
            if (it != endpoint_map.end()) {
                if (!it->second.empty()) {
                    locator.drop_node(uuid);
                }
                endpoint_map.erase(it);
            }
        });
    }
}

auto unicorn_cluster_t::subscriber_t::update_state(std::vector<std::string> nodes) -> void {
    COCAINE_LOG_INFO(log, "received uuid list from zookeeper, got {} uuids", nodes.size());
    subscriptions.apply([&](subscriptions_t& subscriptions) {
        for(const auto& node: nodes) {
            auto it = subscriptions.find(node);
            if(it == subscriptions.end()) {
                auto& subcription = subscriptions[node];
                auto cb = std::bind(&unicorn_cluster_t::subscriber_t::on_node, this, ph::_1);
                subcription.scope = parent.unicorn->subscribe(cb, parent.config.path + '/' + node);
            }
        }
        COCAINE_LOG_INFO(log, "endpoint map now contains {} uuids", subscriptions.size());
    });
}

auto unicorn_cluster_t::subscriber_t::on_children(std::future<response::children_subscribe> future) -> void {
    try {
        update_state(std::get<1>(future.get()));
    } catch (const std::system_error& e) {
        COCAINE_LOG_WARNING(log, "failure during subscription: {} , resubscribing", error::to_string(e));
        timer.defer_retry();
    }
}


unicorn_cluster_t::unicorn_cluster_t(
    cocaine::context_t & _context,
    cocaine::api::cluster_t::interface & _locator,
    mode_t mode,
    const std::string& name,
    const cocaine::dynamic_t& args
):
    cluster_t(_context, _locator, mode, name, args),
    log(_context.log("unicorn")),
    config(args),
    context(_context),
    locator(_locator),
    announcer(this),
    subscriber(this),
    announce_timer(_locator.asio()),
    subscribe_timer(_locator.asio()),
    unicorn(api::unicorn(_context, args.as_object().at("backend").as_string()))
{
    if(mode == mode_t::full) {
        subscriber.subscribe();
    }
    announcer.announce();
}

void unicorn_cluster_t::on_subscribe_timer(const std::error_code& ec) {
    if(!ec) {
        subscribe();
    }
    //else timer was reset somewhere else
}

void unicorn_cluster_t::on_node_list_change(size_t scope_id, std::future<response::children_subscribe> new_list) {
    try {
        auto result = new_list.get();
        auto nodes = std::get<1>(result);
        COCAINE_LOG_INFO(log, "received uuid list from zookeeper, got {} uuids", nodes.size());
        std::sort(nodes.begin(), nodes.end());
        std::vector<std::string> to_delete, to_add;
        std::set<std::string> known_uuids;
        registered_locators.apply([&](locator_endpoints_t& endpoint_map) {
            for (const auto& uuid_to_endpoints : endpoint_map) {
                known_uuids.insert(uuid_to_endpoints.first);
            }
            std::set_difference(known_uuids.begin(),
                                known_uuids.end(),
                                nodes.begin(),
                                nodes.end(),
                                std::back_inserter(to_delete));
            COCAINE_LOG_INFO(log, "{} nodes to drop", to_delete.size());
            for (size_t i = 0; i < to_delete.size(); i++) {
                locator.drop_node(to_delete[i]);
                endpoint_map.erase(to_delete[i]);
            }
            for (auto& uuid: nodes) {
                if(uuid == locator.uuid()) {
                    COCAINE_LOG_DEBUG(log, "skipping local uuid");
                    continue;
                }
                auto it = endpoint_map.find(uuid);
                if(it == endpoint_map.end() || it->second.empty()) {
                    scopes.apply([&](std::map<size_t, api::unicorn_scope_ptr>& _scopes) {
                        auto scope_data = scope(_scopes);
                        auto callback = std::bind(&unicorn_cluster_t::on_node_fetch,
                                                  this,
                                                  scope_data.first,
                                                  uuid,
                                                  std::placeholders::_1);
                        scope_data.second = unicorn->get(std::move(callback),
                                                         config.path + '/' + uuid);
                    });
                } else {
                    locator.link_node(uuid, it->second);
                }
            }
            COCAINE_LOG_INFO(log, "endpoint map now contains {} uuids", endpoint_map.size());
        });
    } catch (const std::system_error& e) {
        drop_scope(scope_id);
        COCAINE_LOG_WARNING(log, "failure during subscription: {} , resubscribing", error::to_string(e));
        subscribe_timer.expires_from_now(boost::posix_time::seconds(config.retry_interval));
        subscribe_timer.async_wait(std::bind(&unicorn_cluster_t::on_subscribe_timer, this, std::placeholders::_1));
    }
}

void unicorn_cluster_t::on_node_fetch(size_t scope_id,
                                      const std::string& uuid,
                                      std::future<api::unicorn_t::response::get> node_endpoints) {
    try {
        drop_scope(scope_id);
        auto result = node_endpoints.get();
        COCAINE_LOG_INFO(log, "fetched {} node's endpoint from zookeeper", uuid);
        std::vector<asio::ip::tcp::endpoint> fetched_endpoints;
        if (result.value().convertible_to<std::vector<asio::ip::tcp::endpoint>>()) {
            fetched_endpoints = result.value().to<std::vector<asio::ip::tcp::endpoint>>();
        }
        if (fetched_endpoints.empty()) {
            COCAINE_LOG_WARNING(log, "invalid value for announce: {}",
                                boost::lexical_cast<std::string>(result.value()).c_str()
            );
            registered_locators.apply([&](locator_endpoints_t& endpoint_map) {
                endpoint_map.erase(uuid);
                locator.drop_node(uuid);
            });
        } else {
            registered_locators.apply([&](locator_endpoints_t& endpoint_map) {
                auto& cur_endpoints = endpoint_map[uuid];
                if (cur_endpoints.empty()) {
                    COCAINE_LOG_INFO(log, "linking {} node", uuid);
                } else {
                    COCAINE_LOG_INFO(log, "relinking {} node", uuid);
                }
                cur_endpoints = fetched_endpoints;
                locator.link_node(uuid, fetched_endpoints);
            });
        }
    } catch (const std::system_error& e) {
        COCAINE_LOG_WARNING(log, "error during fetch: {} ", error::to_string(e));
        registered_locators.apply([&](locator_endpoints_t& endpoint_map) {
            auto it = endpoint_map.find(uuid);
            if (it != endpoint_map.end()) {
                if (!it->second.empty()) {
                    locator.drop_node(uuid);
                }
                endpoint_map.erase(it);
            }
        });
    }
}

void
unicorn_cluster_t::subscribe() {
    scopes.apply([&](std::map<size_t, api::unicorn_scope_ptr>& _scopes) {
        auto scope_data = scope(_scopes);
        scope_data.second = unicorn->children_subscribe(
            std::bind(&unicorn_cluster_t::on_node_list_change, this, scope_data.first, std::placeholders::_1),
            config.path
        );
    });
    subscribe_timer.expires_from_now(boost::posix_time::seconds(config.check_interval));
    subscribe_timer.async_wait(std::bind(&unicorn_cluster_t::on_subscribe_timer, this, std::placeholders::_1));
    COCAINE_LOG_INFO(log, "subscribed for updates on path {} (may be prefixed)", config.path);
}

}}
