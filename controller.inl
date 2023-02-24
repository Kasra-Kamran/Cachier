#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/read.hpp>
#include <boost/endian/arithmetic.hpp>
#include <utility>
#include <map>
#include <iostream>

using namespace asio::experimental::awaitable_operators;
using asio::co_spawn;

template <typename T, typename U>
Controller<T, U>::Controller(std::shared_ptr<boost::asio::io_context> io_ctx, Storage<T, U> storage, std::shared_ptr<Channel> cancellation_channel)
: _storage(std::move(storage))
{
    co_spawn(*io_ctx, accept_loop(cancellation_channel), asio::detached);
}

template <typename T, typename U>
boost::asio::awaitable<void> Controller<T, U>::accept_loop(std::shared_ptr<Channel> channel)
{
    auto ex = co_await asio::this_coro::executor;
    auto kc = std::make_shared<Channel>(ex, 10);
    auto count = std::atomic<int>(1);
    tcp::acceptor acceptor(ex, {{}, 8000});
    tcp::socket sock(ex);
    while(true)
    {
        auto v = co_await (channel->async_receive(asio::use_awaitable) ||
                            acceptor.async_accept(asio::use_awaitable));
        if(v.index())
        {
            count += 1;
            sock = std::move(std::get<1>(v));
            co_spawn(ex, handle_connection(std::move(sock), channel, kc, count), asio::detached);
        }
        else
        {
            std::cout << "breaking now...\n";
            break;
        }
    }

    std::cout << "exited loop.\n";
    while(count > 1)
    {
        std::cout << "waiting to receive message...\n";
        co_await kc->async_receive(asio::use_awaitable);
    }
    std::cout << "accept_loop died.\n";
    co_return;
}

template <typename T, typename U>
asio::awaitable<void> Controller<T, U>::handle_connection(tcp::socket&& stream, std::shared_ptr<Channel> kill_accept_loop, std::shared_ptr<Channel> kc, std::atomic<int>& count)
{
    // Re-read this code.

    std::map<std::string, int> command_list
    {
        {"die", 0},
        {"cancel", 1},
    };

    auto tok = asio::as_tuple(asio::use_awaitable);
    std::cout << "new connection" << std::endl;

    boost::system::error_code ec;
    for (std::string buffer;;) {
        boost::endian::big_uint64_t buffer_size[1];
        size_t n = 0;

        std::tie(ec, std::ignore) = co_await stream.async_receive(asio::buffer(buffer_size), tok);
        if (ec) break;

        std::cout << "message size: " << *buffer_size << std::endl;
        buffer.assign(*buffer_size, '\0');

        std::tie(ec, n) = co_await stream.async_receive(asio::buffer(buffer), tok);

        if (n)
        {
            buffer.resize(n);
            std::cout << "new message: " << buffer << std::endl;
        }

        try
        {
            switch(command_list.at(buffer))
            {
                case 0:
                    std::cout << "killing cache..\n";
                    co_await _storage.kill();
                    break;

                case 1:
                    std::cout << "cancelling accept_loop...\n";
                    co_await kill_accept_loop->async_send(error_code{}, true, asio::use_awaitable);
                    break;
            }
        }
        catch(const std::out_of_range& e){}

        if (ec) break;
    }
    std::cout << "socket closed (" << ec.message() << ")" << std::endl;
    count -= 1;
    co_await kc->async_send(boost::system::error_code{}, true, asio::use_awaitable);
    co_return;
}