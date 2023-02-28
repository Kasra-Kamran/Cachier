#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/endian/arithmetic.hpp>

using namespace asio::experimental::awaitable_operators;
using asio::co_spawn;

Comms::Comms(asio::io_context& io_ctx, Channel& cancellation_channel, std::size_t port, UChannel<std::string>& msg_channel, UChannel<std::string>& response_channel)
{
    co_spawn(io_ctx, accept_loop(cancellation_channel, port, msg_channel, response_channel), asio::detached);
}

asio::awaitable<void> Comms::accept_loop(Channel& channel, std::size_t port, UChannel<std::string>& msg_channel, UChannel<std::string>& response_channel)
{
    auto ex = co_await asio::this_coro::executor;
    Channel kc(ex, 1);
    auto kc_count = std::atomic<int>(1);
    tcp::acceptor acceptor(ex, {{}, port});
    tcp::socket sock(ex);
    while(true)
    {
        auto v = co_await (channel.async_receive(asio::use_awaitable) ||
                            acceptor.async_accept(asio::use_awaitable));
        if(v.index())
        {
            kc_count += 1;
            sock = std::move(std::get<1>(v));
            co_spawn(ex, handle_connection(std::move(sock), channel, kc, kc_count, msg_channel, response_channel), asio::detached);
        }
        else
        {
            std::cout << "breaking now...\n";
            break;
        }
    }

    std::cout << "exited loop.\n";
    while(kc_count > 1)
    {
        std::cout << "waiting to receive message...\n";
        co_await kc.async_receive(asio::use_awaitable);
    }
    std::cout << "accept_loop died.\n";
    co_return;
}

asio::awaitable<void> Comms::handle_connection(tcp::socket&& stream, Channel& kill_accept_loop, Channel& kc, std::atomic<int>& kc_count, UChannel<std::string>& msg_channel, UChannel<std::string>& response_channel)
{
    // Re-read this code.
    auto ex = co_await asio::this_coro::executor;
    Channel kill_response(ex, 1);
    co_spawn(ex, respond(stream, response_channel, kill_response), asio::detached);
    auto tok = asio::as_tuple(asio::use_awaitable);
    std::map<std::string, int> comms_commands
    {
        {"disable", 0},
        {"enable", 1},
    };
    bool k = true;
    bool u = true;
    boost::system::error_code ec;
    for (std::string buffer;;)
    {
        boost::endian::big_uint64_t buffer_size[1];
        size_t n = 0;

        std::tie(ec, std::ignore) = co_await stream.async_receive(asio::buffer(buffer_size), tok);
        if (ec) break;

        buffer.assign(*buffer_size, '\0');

        std::tie(ec, n) = co_await stream.async_receive(asio::buffer(buffer), tok);

        if (n)
        {
            buffer.resize(n);
            std::cout << "new message: " << buffer << std::endl;
        }

        // make try + switch block to handle these, with
        // the default case being sending it to "msg_channel".
        
        if(u && buffer == "kill_accept_loop")
        {
            u = false;
            co_await kill_accept_loop.async_send(boost::system::error_code{}, true, asio::use_awaitable);
        }
        if(k)
        {
            if(buffer == "cancel")
                k = false;
            co_await msg_channel.async_send(boost::system::error_code{}, buffer, asio::use_awaitable);
        }

        if (ec) break;
    }
    kc_count -= 1;
    // it's and doesn't work all the time. fix it.
    co_await (kc.async_send(boost::system::error_code{}, true, asio::use_awaitable) &&
              kill_response.async_send(boost::system::error_code{}, true, asio::use_awaitable));
    std::cout << "handler died\n";
    co_return;
}

asio::awaitable<void> Comms::respond(tcp::socket& stream, UChannel<std::string>& response_channel, Channel& kill_response)
{
    auto coroutine = [](tcp::socket& stream, UChannel<std::string>& response_channel) -> asio::awaitable<void>
    {
        for(std::string response;;)
        {
            response = co_await response_channel.async_receive(asio::use_awaitable);
            co_await stream.async_send(asio::buffer(std::to_string(response.size())), asio::use_awaitable);
            co_await stream.async_send(asio::buffer(response), asio::use_awaitable);
        }
        co_return;
    };
    co_await (coroutine(stream, response_channel) ||
              kill_response.async_receive(asio::use_awaitable));
    co_return;
}