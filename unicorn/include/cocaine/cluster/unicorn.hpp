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

#pragma once

#include <cocaine/api/cluster.hpp>
#include <cocaine/api/unicorn.hpp>
#include <cocaine/forwards.hpp>
#include <cocaine/locked_ptr.hpp>

#include <set>
namespace cocaine { namespace cluster {

class unicorn_cluster_t:
    public api::cluster_t
{
public:
    typedef api::unicorn_t::response response;

    struct cfg_t {
        cfg_t(const dynamic_t& args);

        unicorn::path_t path;
        size_t retry_interval;
        size_t check_interval;
    };

    class timer_t {
        unicorn_cluster_t& parent;
        synchronized<asio::deadline_timer> timer;
        std::function<void()> callback;

    public:
        timer_t(unicorn_cluster_t& parent, std::function<void()> callback);
        auto defer_retry() -> void;
        auto defer_check() -> void;

    private:
        auto defer(const boost::posix_time::time_duration& duration) -> void;
    };

    class announcer_t {
        unicorn_cluster_t& parent;

        timer_t timer;
        std::string path;
        std::vector<asio::ip::tcp::endpoint> endpoints;

        boost::optional<api::auto_scope_t> scope;

    public:
        announcer_t(unicorn_cluster_t& parent);

        auto announce() -> void;

    private:
        auto on_set(std::future<response::create> future) -> void;

        auto on_check(std::future<response::subscribe> future) -> void;

        auto set_and_check_endpoints() -> bool;

        auto set_timer(const boost::posix_time::time_duration& duration) -> void;

        auto defer_announce_retry() -> void;
    };

    class subscriber_t {
        unicorn_cluster_t& parent;
        timer_t timer;

        struct locator_subscription_t {
            std::vector<asio::ip::tcp::endpoint> endpoints;
            boost::optional<auto_scope_t> scope;
        };

        using subscriptions_t = std::map<std::string, locator_subscription_t>;
        synchronized<subscriptions_t> subscriptions;

//    public:
//        using node_scopes_t = std::map<std::string, auto_scope_t>;
//
    private:
        unicorn_cluster_t& parent;

        auto_scope_t children_scope;
//
//        node_scopes_t node_scopes;

    public:
        subscriber_t(unicorn_cluster_t& parent);

        auto subscribe() -> void;

    private:
        auto on_children(std::future<response::children_subscribe> future) -> void;
        auto on_node(std::string uuid, std::future<response::subscribe> future) -> void;
        auto update_state(std::vector<std::string> nodes) -> void;
    };


    unicorn_cluster_t(context_t& context, interface& locator, mode_t mode, const std::string& name, const dynamic_t& args);

private:
    std::pair<size_t, api::unicorn_scope_ptr&>
    scope(std::map<size_t, api::unicorn_scope_ptr>& _scopes);

    void
    drop_scope(size_t);

    void
    announce();

    void
    subscribe();

    void
    on_announce_timer(const std::error_code& ec);

    void
    on_subscribe_timer(const std::error_code& ec);

    void
    on_node_list_change(size_t scope_id, std::future<response::children_subscribe> new_list);

    void
    on_node_fetch(size_t scope_id, const std::string& uuid, std::future<response::get> node_endpoints);

    void
    on_announce_set(std::future<response::create> future);

    void
    on_announce_checked(std::future<response::subscribe> future);

    std::shared_ptr<logging::logger_t> log;
    cfg_t config;
    cocaine::context_t& context;
    cluster_t::interface& locator;
    announcer_t announcer;
    subscriber_t subscriber;
    std::vector<asio::ip::tcp::endpoint> endpoints;
    asio::deadline_timer announce_timer;
    asio::deadline_timer subscribe_timer;

    api::unicorn_ptr unicorn;
    api::unicorn_scope_ptr subscribe_scope;
    api::unicorn_scope_ptr announce_scope;
    api::unicorn_scope_ptr check_scope;
//    std::atomic<size_t> scope_counter;
    using scopes_t = std::map<std::string, api::unicorn_scope_ptr>;
    synchronized<scopes_t> scopes;

    typedef std::map<std::string, std::vector<asio::ip::tcp::endpoint>> locator_endpoints_t;
    synchronized<locator_endpoints_t> registered_locators;
};

}}
