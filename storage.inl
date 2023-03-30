#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/bind/bind.hpp>
#include <utility>
#include <new>
#include <map>

#include <boost/asio/deadline_timer.hpp>
#include <iostream>

using asio::co_spawn;

template <typename T, typename U>
Storage<T, U>::Storage(Storage<T, U>&& s)
{
    this->_outgoing = s._outgoing;
};

template <typename T, typename U>
Storage<T, U>::Storage(asio::io_service& io_ctx, std::size_t num_caches, UChannel<IdMessage>& msg_channel, UChannel<IdMessage>& response_channel)
{
    for(int i = 0; i < num_caches; i++)
    {
        _warehouse.push_back(std::unordered_map<U, T>());
    }
    for(int i = 0; i < num_caches; i++)
    {
        auto channel = std::make_shared<UChannel<Message<T, U>>>(io_ctx, sizeof(T));
        _outgoing.push_back(channel);
        co_spawn(io_ctx, cache(std::move(_warehouse[i]), *channel), asio::detached);
    }
    co_spawn(io_ctx, handle_storage_commands(msg_channel, response_channel), asio::detached);
    std::cout << "main ended\n";
}

// "cache" should not handle cases where the entry is
// in the DB instead of the cache; it should spawn a coroutine
// that waits for a respose from the DB and passes it
// to "get".
template <typename T, typename U>
asio::awaitable<void> Storage<T, U>::cache(std::unordered_map<U, T>&& box, UChannel<Message<T, U>>& incoming)
{
    bool not_dead = true;
    while(not_dead)
    {
        Message<T, U> msg = co_await incoming.async_receive(asio::use_awaitable);
        switch(msg.command)
        {
            case Insert:
                if(!msg.data)
                    break;
                for(auto record : *msg.data)
                    box.insert({std::get<0>(record), std::get<1>(record)});
                break;

            case Die:
                not_dead = false;
                std::cout << "cache died\n";
                break;

            case Get:
                try
                {
                    // fix this it's shit and will break.
                    T value = box.at(*(msg.key));
                    new (**msg.return_value) T(value);
                    // make this async_send.
                    co_await incoming.async_receive(asio::use_awaitable);
                }
                catch(const std::out_of_range& e)
                {
                    msg.return_value->reset();
                }
                break;
        }
    }
    co_return;
}

template <typename T, typename U>
asio::awaitable<void> Storage<T, U>::handle_storage_commands(UChannel<IdMessage>& msg_channel, UChannel<IdMessage>& response_channel)
{
    auto ex = co_await asio::this_coro::executor;
    auto send_error = [](UChannel<IdMessage>& response_channel, IdMessage msg) -> asio::awaitable<void>
    {
        co_await response_channel.async_send(boost::system::error_code{}, msg, asio::use_awaitable);
    };
    std::map<std::string, int> commands
    {
        {"cancel", 0},
        {"die", 1},
        {"get", 2},
        {"insert", 3},
    };
    bool alive = true;
    IdMessage m;
    json j;
    // has bug with incoming message size or something.
    while(alive)
    {
        m = co_await msg_channel.async_receive(asio::use_awaitable);
        try
        {
            j = json::parse(m.message);
        }
        catch(const json::parse_error& pe)
        {
            json inv = CACHE_INVALID;
            m.message = inv.dump();
            co_spawn(ex, send_error(response_channel, m), asio::detached);
            continue;
        }
        catch(const std::exception& e)
        {
            std::cout << __LINE__ << " : " << e.what() << "\n";
            continue;
        }
        std::cout << "received new message from comms\n";
        try
        {
            // respond in the cases of the switch block using "response_channel".
            switch(commands.at(j["command"]))
            {
                case 0:
                    alive = false;
                    std::cout << "unaliving the handle_storage_commands\n";
                    break;
                case 1:
                    co_await kill();
                    std::cout << "killing storage\n";
                    break;

                case 2:
                    co_spawn(ex, [this](UChannel<IdMessage>& response_channel, json& j, IdMessage& msg) -> asio::awaitable<void>
                    {
                        try
                        {
                            U k = U::from_json(j);
                            std::optional<T> res = co_await get(k);
                            json v;
                            if(res.has_value())
                                v = T::to_json(*res);
                            else
                                v = CACHE_MISS;
                            v["request"] = j["request"];
                            msg.message = v.dump();
                            co_await response_channel.async_send(boost::system::error_code{}, msg, asio::use_awaitable);
                            co_return;
                        }
                        catch(const json::type_error& te)
                        {
                            json a = CACHE_MISS;
                            a["request"] = j["request"];
                            msg.message = a.dump();
                            co_await response_channel.async_send(boost::system::error_code{}, msg, asio::use_awaitable);
                        }
                        catch(const std::exception& e)
                        {
                            std::cout << __LINE__ << " : " << e.what() << "\n";
                        }
                    }(response_channel, j, m), asio::detached);
                    break;
                case 3:
                    co_spawn(ex, [this](UChannel<IdMessage>& response_channel, json& j, IdMessage& msg) -> asio::awaitable<void>
                    {
                        try
                        {
                            U k = U::from_json(j);
                            T v = T::from_json(j);
                            std::vector<std::tuple<U, T>> data;
                            data.push_back(std::make_tuple(k, v));
                            co_await insert(data);
                            json b = CACHE_INSERTED;
                            b["request"] = j["request"];
                            msg.message = b.dump();
                            co_await response_channel.async_send(boost::system::error_code{}, msg, asio::use_awaitable);
                            co_return;
                        }
                        catch(const json::type_error& te)
                        {
                            json a = CACHE_INVALID_KEY_OR_VALUE;
                            a["request"] = j["request"];
                            msg.message = a.dump();
                            co_await response_channel.async_send(boost::system::error_code{}, msg, asio::use_awaitable);
                        }
                        catch(const std::exception& e)
                        {
                            std::cout << __LINE__ << " : " << e.what() << "\n";
                        }
                    }(response_channel, j, m), asio::detached);
                    break;
            }
        }
        catch(const json::type_error& te)
        {
            json a = CACHE_INVALID;
            a["request"] = j["request"];
            m.message = a.dump();
            co_spawn(ex, send_error(response_channel, m), asio::detached);
        }
        catch(const std::out_of_range& ofr)
        {
            json a = CACHE_UNKNOWN_COMMAND;
            a["request"] = j["request"];
            m.message = a.dump();
            co_spawn(ex, send_error(response_channel, m), asio::detached);
        }
    }
    co_return;
}

// Should get an io object as an argument,
// create a coroutine and pass the io object
// and the receiving end of a channel
// communicating with "cache" to it.
// This coroutine waits for a response
// from the "cache" and sends it to
// the io object.
//
// OR (the seconds one is better)
//
// It should return an awaitable
// and let a higher layer handle sending
// the result to the client,
// which means "get" should be spawned as
// a coroutine.
// Also you should be able to spawn
// multiple "get"s.
template <typename T, typename U>
asio::awaitable<std::optional<T>> Storage<T, U>::get(U key)
{
    std::size_t hash = hasher(key);
    // fix this! it's leaking memory like a sieve leaks water!
    // Allocate on the stack, not on the heap.
    // or maybe do a single big heap allocation at the beginning of the program
    // and use that.
    // std::optional<T*> return_value = (T*)malloc(sizeof(T));
    std::optional<T*> return_value = (T*)_p.get();
    Message<T, U> m;
    m.command = Get;
    m.key = key;
    m.return_value = &return_value;
    UChannel<Message<T, U>>& ch = *_outgoing[hash % _outgoing.size()];
    co_await ch.async_send(boost::system::error_code{}, m, asio::use_awaitable);
    Message<T, U> p;
    p.command = Ping;
    co_await ch.async_send(boost::system::error_code{}, p, asio::use_awaitable);
    if(return_value.has_value())
    {
        // std::cout << **return_value << "\n";
        T r(**return_value);
        _p.free(*return_value);
        co_return r;
    }
    co_return std::optional<T>{};
}

template <typename T, typename U>
asio::awaitable<void> Storage<T, U>::insert(std::vector<std::tuple<U, T>> entries)
{
    std::size_t num_caches = _outgoing.size();
    std::vector<std::tuple<U, T>> data;
    std::size_t length_entries;
    for(int i = 0; i < num_caches; i++)
    {
        Message<T, U> m;
        m.command = Insert;

        length_entries = entries.size();
        for(int j = 0; j < length_entries; j++)
        {
            if(hasher(std::get<0>(entries[j])) % num_caches == i)
            {
                data.push_back(entries[j]);
                entries.erase(std::next(entries.begin(), j));
                j -= 1;
                length_entries -= 1;
            }
        }
        m.data = data;
        data.clear();
        co_await _outgoing[i]->async_send(error_code{}, m, asio::use_awaitable);
    }
    co_return;
}

template <typename T, typename U>
asio::awaitable<void> Storage<T, U>::kill()
{
    Message<T, U> m;
    m.command = Die;
    for(auto& channel : _outgoing)
        co_await channel->async_send(boost::system::error_code{}, m, asio::use_awaitable);
    std::cout << "kill ran\n";
    co_return;
}
