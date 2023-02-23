#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/read.hpp>
#include <utility>
#include <map>
#include <iostream>

using namespace asio::experimental::awaitable_operators;
using asio::co_spawn;

template <typename T, typename U>
Controller<T, U>::Controller(std::shared_ptr<boost::asio::io_context> io_ctx, Storage<T, U> storage, std::shared_ptr<Channel> cancellation_channel)
: _storage(std::move(storage))
{
    co_spawn(*io_ctx, accept_loop(io_ctx, cancellation_channel), asio::detached);
}

template <typename T, typename U>
boost::asio::awaitable<void> Controller<T, U>::accept_loop(std::shared_ptr<asio::io_context> io_ctx, std::shared_ptr<Channel> channel)
{
    auto cancellable = [this, io_ctx, channel]() -> asio::awaitable<void>
    {
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), 8000);
        boost::asio::ip::tcp::acceptor acceptor(*io_ctx, endpoint);
        tcp::socket sock = co_await acceptor.async_accept(asio::use_awaitable);
        while(true)
        {
            co_spawn(*io_ctx, handle_connection(std::move(sock), channel), asio::detached);
            sock = co_await acceptor.async_accept(asio::use_awaitable);
        }
        co_return;
    };
    co_await (cancellable() ||
              channel->async_receive(asio::use_awaitable));
    std::cout << "accept_loop died.\n";
    co_return;
}

template <typename T, typename U>
asio::awaitable<void> Controller<T, U>::handle_connection(tcp::socket&& stream, std::shared_ptr<Channel> channel)
{
    std::cout << "new connection\n";
    std::string buffer;
    uint64_t buffer_size;

    std::map<std::string, int> s
    {
        {"die", 0},
        {"cancel", 1},
    };

    while(stream.is_open())
    {
        // Our next problems:
        //      async_receive/async_read doesn't know socket is closed.
        //      "accept_loop" should somehow wait for "handle_conneciton"
        //      to finish after being "cancel"ed.

        // co_await stream.async_receive(asio::buffer(&buffer_size, 8), asio::use_awaitable);
        co_await asio::async_read(stream, asio::buffer(&buffer_size, 8), asio::use_awaitable);
        buffer = std::string(buffer_size, '\0');
        std::cout << "message size: " << buffer_size << "\n";
        // co_await stream.async_receive(asio::buffer(buffer, buffer_size), asio::use_awaitable);
        co_await asio::async_read(stream, asio::buffer(buffer), asio::use_awaitable);
        std::cout << "new message: " << buffer << "\n";

        try
        {
            int a = s.at(buffer);
            switch(a)
            {
                case 0:
                    std::cout << "killing cache..\n";
                    co_await _storage.kill();
                    break;

                case 1:
                    std::cout << "cancelling accept_loop...\n";
                    co_await channel->async_send(error_code{}, std::string("hello"), asio::use_awaitable);
                    break;
            }
        }
        catch(const std::out_of_range& e){std::cout << "entry does not exist in map.\n";}
    }
    std::cout << "connection terminated\n";
    co_return;
}