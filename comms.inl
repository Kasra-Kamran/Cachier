#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/endian/arithmetic.hpp>

using namespace asio::experimental::awaitable_operators;
using asio::co_spawn;

Comms::Comms(asio::io_context& io_ctx, Channel& cancellation_channel, std::size_t port, UChannel<std::string>& msg_channel, UChannel<std::string>& response_channel)
{
    auto relay_channel = std::make_shared<UChannel<std::tuple<std::shared_ptr<UChannel<std::string>>, std::shared_ptr<UChannel<std::string>>>>>(io_ctx, 3);
    auto kill_relay = std::make_shared<Channel>(io_ctx, 1);
    co_spawn(io_ctx, relay(msg_channel, response_channel, relay_channel, kill_relay), asio::detached);
    co_spawn(io_ctx, accept_loop(cancellation_channel, port, relay_channel, kill_relay), asio::detached);
}

struct IdMessage
{
    std::atomic<int> id;
    std::string message;
};

asio::awaitable<void> Comms::relay(
    UChannel<std::string>& msg_channel,
    UChannel<std::string>& response_channel, 
    std::shared_ptr<UChannel<std::tuple<std::shared_ptr<UChannel<std::string>>, 
    std::shared_ptr<UChannel<std::string>>>>> relay_channel, 
    std::shared_ptr<Channel> kill_relay)
{
    auto ex = co_await asio::this_coro::executor;
    std::shared_ptr<UChannel<std::string>> m;
    std::shared_ptr<UChannel<std::string>> r;
    // UChannel<std::string>* m;
    // UChannel<std::string>* r;
    while(true)
    {
        auto v = co_await (kill_relay->async_receive(asio::use_awaitable) ||
                           relay_channel->async_receive(asio::use_awaitable));
        if(v.index())
        {
            std::tie(m, r) = std::get<1>(v);
            auto kill_responder = std::make_shared<Channel>(ex, 1);
            co_spawn(ex, [](UChannel<std::string>& msg_channel, UChannel<std::string>& response_channel, std::shared_ptr<UChannel<std::string>> m, std::shared_ptr<UChannel<std::string>> r, std::shared_ptr<Channel> kill_responder) -> asio::awaitable<void>
            {
                // This should get killed by receiving a message
                // from the below coroutine.(through a channel allocated
                // by relay and passed to these two)
                for(std::string response;;)
                {
                    auto v = co_await (kill_responder->async_receive(asio::use_awaitable) ||
                                       response_channel.async_receive(asio::use_awaitable));
                    if(v.index())
                    {
                        co_await r->async_send(boost::system::error_code{}, std::get<1>(v), asio::use_awaitable);
                    }
                    else
                    {
                        break;
                    }
                }
                co_return;
            }(msg_channel, response_channel, m, r, kill_responder), asio::detached);
            co_spawn(ex, [](UChannel<std::string>& msg_channel, UChannel<std::string>& response_channel, std::shared_ptr<UChannel<std::string>> m, std::shared_ptr<UChannel<std::string>> r, std::shared_ptr<Channel> kill_responder) -> asio::awaitable<void>
            {
                for(std::string message;;)
                {
                    message = co_await m->async_receive(asio::use_awaitable);
                    co_await msg_channel.async_send(boost::system::error_code{}, message, asio::use_awaitable);
                    if(message == "cancel")
                    {
                        co_await kill_responder->async_send(boost::system::error_code{}, true, asio::use_awaitable);
                        break;
                    }
                }
                co_return;
            }(msg_channel, response_channel, m, r, kill_responder), asio::detached);
        }
        else
        {
            break;
        }
    }
    co_return;
}

asio::awaitable<void> Comms::accept_loop(
    Channel& channel,
    std::size_t port,
    std::shared_ptr<UChannel<std::tuple<std::shared_ptr<UChannel<std::string>>,
    std::shared_ptr<UChannel<std::string>>>>> relay_channel,
    std::shared_ptr<Channel> kill_relay)
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
            // Allocate on the stack!
            // UChannel<std::string>* mc = (UChannel<std::string>*)alloca(sizeof(UChannel<std::string>));
            // UChannel<std::string>* rc = (UChannel<std::string>*)alloca(sizeof(UChannel<std::string>));
            // new (mc) UChannel<std::string>(ex, 10);
            // new (rc) UChannel<std::string>(ex, 10);
            // UChannel<std::string> mc(ex, 10);
            // UChannel<std::string> rc(ex, 10);
            // co_spawn(ex, f(amc, arc), asio::detached);
            // co_spawn(ex, g(amc, arc), asio::detached);
            // FIX THIS! FREE THE MEMORY MAN IT'S LEAKING!
            // UChannel<std::string>* mc = (UChannel<std::string>*)malloc(sizeof(UChannel<std::string>));
            // UChannel<std::string>* rc = (UChannel<std::string>*)malloc(sizeof(UChannel<std::string>));
            // new (mc) UChannel<std::string>(ex, 10);
            // new (rc) UChannel<std::string>(ex, 10);
            auto mc = std::make_shared<UChannel<std::string>>(ex, 10);
            auto rc = std::make_shared<UChannel<std::string>>(ex, 10);
            kc_count += 1;
            sock = std::move(std::get<1>(v));
            co_spawn(ex, handle_connection(std::move(sock), channel, kc, kc_count, *mc, *rc), asio::detached);
            co_spawn(ex, [](std::shared_ptr<UChannel<std::tuple<std::shared_ptr<UChannel<std::string>>, std::shared_ptr<UChannel<std::string>>>>> relay_channel, std::shared_ptr<UChannel<std::string>> m,  std::shared_ptr<UChannel<std::string>> r) -> asio::awaitable<void>
            {
                co_await relay_channel->async_send(boost::system::error_code{}, std::make_tuple(m, r), asio::use_awaitable);
                co_return;
            }(relay_channel, mc, rc), asio::detached);
        }
        else
        {
            std::cout << "breaking now...\n";
            break;
        }
    }

    std::cout << "exited loop.\n";
    co_await kill_relay->async_send(boost::system::error_code{}, true, asio::use_awaitable);
    while(kc_count > 1)
    {
        std::cout << "waiting to receive message...\n";
        co_await kc.async_receive(asio::use_awaitable);
    }
    std::cout << "accept_loop died.\n";
    co_return;
}

// fix this response channel shit, the responder doesn't work!
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
            std::cout << "new message: " << buffer << "\n";
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
    co_await msg_channel.async_send(boost::system::error_code{}, "cancel", asio::use_awaitable);
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
            // make this a tuple or something.
            co_await stream.async_send(asio::buffer(std::to_string(response.size())), asio::use_awaitable);
            co_await stream.async_send(asio::buffer(response), asio::use_awaitable);
        }
        co_return;
    };
    co_await (coroutine(stream, response_channel) ||
              kill_response.async_receive(asio::use_awaitable));
    co_return;
}