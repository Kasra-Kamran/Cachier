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
Storage<T, U>::Storage(asio::io_service& io_ctx, std::size_t num_caches, UChannel<std::string>& msg_channel, UChannel<std::string>& response_channel)
{
    for(int i = 0; i < num_caches; i++)
    {
        _warehouse.push_back(std::unordered_map<U, T>());
    }
    for(int i = 0; i < num_caches; i++)
    {
        auto channel = std::make_shared<UChannel<Message<T, U>>>(io_ctx, sizeof(T));
        _outgoing.push_back(channel);
        co_spawn(io_ctx, cache<T, U>(std::move(_warehouse[i]), *channel), asio::detached);
    }
    co_spawn(io_ctx, handle_storage_commands(msg_channel, response_channel, *this), asio::detached);
    std::cout << "main ended\n";
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
    std::optional<T*> return_value = (T*)malloc(sizeof(T));
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
        co_return **return_value;
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

template <typename T, typename U>
asio::awaitable<void> handle_storage_commands(UChannel<std::string>& msg_channel, UChannel<std::string>& response_channel, Storage<T, U>& storage)
{
    std::map<std::string, int> commands
    {
        {"cancel", 0},
        {"die", 1},
    };
    bool alive = true;
    std::string message;
    while(alive)
    {
        message = co_await msg_channel.async_receive(asio::use_awaitable);
        std::cout << "received new message from comms\n";
        try
        {
            // respond in the cases of the switch block using "response_channel".
            switch(commands.at(message))
            {
                case 0:
                    alive = false;
                    std::cout << "unaliving the handle_storage_commands\n";
                    break;
                case 1:
                    co_await storage.kill();
                    std::cout << "killing storage\n";
                    break;
            }
        }
        catch(const std::out_of_range& e){}
    }
}

// Things get fucked when you use a template class method.
// "cache" should not handle cases where the entry is
// in the DB instead of the cache; it should spawn a coroutine
// that waits for a respose from the DB and passes it
// to "get".
template <typename T, typename U>
boost::asio::awaitable<void> cache(std::unordered_map<U, T>&& box, UChannel<Message<T, U>>& incoming)
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
                    T value = box.at(*(msg.key));
                    new (**msg.return_value) T(value);
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