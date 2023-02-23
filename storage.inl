#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/bind/bind.hpp>
#include <utility>

#include <boost/asio/deadline_timer.hpp>
#include <iostream>

using asio::co_spawn;

// Things get fucked when you use a template class method.
// "cache" should not handle cases where the entry is
// in the DB instead of the cache; it should spawn a coroutine
// that waits for a respose from the DB and passes it
// to "get".
template <typename T, typename U>
boost::asio::awaitable<void> cache(std::unordered_map<U, T>&& box, std::shared_ptr<UChannel<Message<T, U>>> incoming)
{
    // std::cout << "dear god why\n";
    // co_await incoming->async_receive(asio::use_awaitable_t());
    // std::cout << "received message.\n";
    bool not_dead = true;
    while(not_dead)
    {
        Message msg = co_await incoming->async_receive(asio::use_awaitable);
        switch(msg.command)
        {
            case Insert:
                if(!msg.data)
                    break;
                std::cout << "inserting stuff into cache\n";
                for(auto record : *msg.data)
                {
                    box.insert({get<0>(record), get<1>(record)});
                }
                break;

            case Die:
                not_dead = false;
                std::cout << "cache died\n";
                break;
        }
    }
    co_return;
}

template <typename T, typename U>
Storage<T, U>::Storage(Storage<T, U>&& s)
{
    this->_outgoing = s._outgoing;
};

template <typename T, typename U>
Storage<T, U>::Storage(std::shared_ptr<asio::io_service> io_ctx, std::size_t num_caches)
{
    for(int i = 0; i < num_caches; i++)
    {
        // auto map = make_shared<unordered_map<U, T>>();
        _warehouse.push_back(std::unordered_map<U, T>());
    }
    for(int i = 0; i < num_caches; i++)
    {
        auto channel = make_shared<UChannel<Message<T, U>>>(*io_ctx, sizeof(T));
        _outgoing.push_back(channel);
        co_spawn(*io_ctx, cache<T, U>(std::move(_warehouse[i]), channel), asio::detached);
    }
    co_spawn(*io_ctx, [this, io_ctx]() -> asio::awaitable<void>
    {
        // just send random shit into the channels.
        U k;
        k.id = 5;
        k.date = 1.5;
        Message<T, U> m;
        m.key = k;
        m.command = Get;
        for(int i = 0; i < 5; i++)
            co_await this->_outgoing[i]->async_send(error_code{}, m, asio::use_awaitable);
        // boost::asio::deadline_timer timer(*io_ctx);
        // timer.expires_from_now(boost::posix_time::seconds(2));
        // co_await timer.async_wait(boost::asio::use_awaitable_t());
        std::cout << "sender coroutine ended\n";
        co_return;
    }, boost::asio::detached);
    std::cout << "main ended\n";
}

// Things get fucked when you use a template class method.
// template <typename T, typename U>
// awaitable<void> Storage<T, U>::cache(unordered_map<U, T> box, shared_ptr<experimental::concurrent_channel<void(boost::system::error_code, U)>> incoming)
// {    
//     std::cout << "dear god why";
//     co_await incoming->async_receive(use_awaitable_t());
//     co_return;
// }


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
    std::hash<U> hasher;
    std::size_t hash = hasher(key);
    std::cout << hash << "\n";
    std::optional<T> o;
    co_return o;
}

template <typename T, typename U>
asio::awaitable<void> Storage<T, U>::insert(std::vector<std::tuple<U, T>> entries)
{
    std::hash<U> hasher;
    std::size_t num_caches = _outgoing.size();
    for(int i = 0; i < num_caches; i++)
    {
        Message<T, U> m;
        m.command = Insert;
        std::vector<std::tuple<U, T>> data;

        std::size_t length_entries = entries.size();
        // for (std::vector<int>::iterator it = c.begin(); it != c.end();)
        // {

        // }
        for(int j = 0; i < length_entries; i++)
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
        co_await _outgoing[i]->async_send(error_code{}, m, asio::use_awaitable);
    }
    co_return;
}

template <typename T, typename U>
asio::awaitable<void> Storage<T, U>::kill()
{
    // co_spawn()
    // for(auto channel : _outgoing)
    // {
    //     co_await channel-
    // }
    std::cout << "kill ran\n";
    co_return;
}