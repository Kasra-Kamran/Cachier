#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/endian/arithmetic.hpp>
#include <unordered_map>
#include <vector>

using namespace asio::experimental::awaitable_operators;
using asio::co_spawn;

Comms::Comms(asio::io_context& io_ctx, Channel& cancellation_channel, std::size_t port, UChannel<IdMessage>& msg_channel, UChannel<IdMessage>& response_channel)
{
    auto relay_channel = std::make_shared<UChannel<std::tuple<std::shared_ptr<UChannel<IdMessage>>, std::tuple<int, std::shared_ptr<UChannel<std::string>>>>>>(io_ctx, 3);
    auto kill_relay = std::make_shared<Channel>(io_ctx, 1);
    co_spawn(io_ctx, relay(msg_channel, response_channel, relay_channel, kill_relay), asio::detached);
    co_spawn(io_ctx, accept_loop(cancellation_channel, port, relay_channel, kill_relay), asio::detached);
}

asio::awaitable<void> Comms::relay(
    UChannel<IdMessage>& msg_channel,
    UChannel<IdMessage>& response_channel, 
    std::shared_ptr<UChannel<std::tuple<std::shared_ptr<UChannel<IdMessage>>, 
    std::tuple<int, std::shared_ptr<UChannel<std::string>>>>>> relay_channel, 
    std::shared_ptr<Channel> kill_relay)
{
    auto ex = co_await asio::this_coro::executor;
    std::shared_ptr<UChannel<IdMessage>> m;
    std::tuple<int, std::shared_ptr<UChannel<std::string>>> r;

    Channel kill_responder(ex, 1);

    UChannel<int> remove_channel(ex, 5);
    UChannel<std::tuple<int, std::shared_ptr<UChannel<std::string>>>> channelchannel(ex, 5);
    co_spawn(ex, [](UChannel<std::tuple<int, std::shared_ptr<UChannel<std::string>>>>& channelchannel,
    UChannel<IdMessage>& response_channel,
    UChannel<int>& remove_channel,
    Channel& kill_responder) -> asio::awaitable<void>
    {
        std::unordered_map<int, std::shared_ptr<UChannel<std::string>>> list_channels;
        IdMessage m;
        int num;
        std::shared_ptr<UChannel<std::string>> channel;
        bool alive = true;
        while(alive)
        {
            auto v = co_await (channelchannel.async_receive(asio::use_awaitable) ||
                               response_channel.async_receive(asio::use_awaitable) ||
                               remove_channel.async_receive(asio::use_awaitable) ||
                               kill_responder.async_receive(asio::use_awaitable));

            switch(v.index())
            {
                case 0:
                    std::tie(num, channel) = std::get<0>(v);
                    list_channels.try_emplace(num, channel);
                    break;
                case 1:
                    m = std::get<1>(v);
                    if(list_channels.contains(m.id))
                        co_await list_channels[m.id]->async_send(boost::system::error_code{}, m.message, asio::use_awaitable);
                    break;
                case 2:
                    list_channels.erase(std::get<2>(v));
                    break;
                case 3:
                    alive = false;
                    break;
            }
        }
    }(channelchannel, response_channel, remove_channel, kill_responder), asio::detached);

    while(true)
    {
        auto v = co_await (kill_relay->async_receive(asio::use_awaitable) ||
                           relay_channel->async_receive(asio::use_awaitable));
        if(v.index())
        {
            std::tie(m, r) = std::get<1>(v);
            
            co_await channelchannel.async_send(boost::system::error_code{}, r, asio::use_awaitable);

            co_spawn(ex, [](UChannel<IdMessage>& msg_channel, std::shared_ptr<UChannel<IdMessage>> m,
            UChannel<int>& remove_channel) -> asio::awaitable<void>
            {
                int id;
                for(IdMessage message;;)
                {
                    message = co_await m->async_receive(asio::use_awaitable);
                    id = message.id;
                    if(message.message == "cancel")
                    {
                        co_return;
                    }
                    co_await msg_channel.async_send(boost::system::error_code{}, message, asio::use_awaitable);

                }
                co_await remove_channel.async_send(boost::system::error_code{}, id, asio::use_awaitable);
                co_return;
            }(msg_channel, m, remove_channel), asio::detached);
        }
        else
        {
            break;
        }
    }
    co_await kill_responder.async_send(boost::system::error_code{}, true, asio::use_awaitable);
    co_return;
}

asio::awaitable<void> Comms::accept_loop(
    Channel& channel,
    std::size_t port,
    std::shared_ptr<UChannel<std::tuple<std::shared_ptr<UChannel<IdMessage>>,
    std::tuple<int, std::shared_ptr<UChannel<std::string>>>>>> relay_channel,
    std::shared_ptr<Channel> kill_relay)
{
    auto ex = co_await asio::this_coro::executor;
    Channel kc(ex, 1);
    auto kc_count = std::atomic<int>(0);
    tcp::acceptor acceptor(ex, {{}, port});

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
            auto mc = std::make_shared<UChannel<IdMessage>>(ex, 10);
            auto rc = std::make_shared<UChannel<std::string>>(ex, 10);
            auto sock = std::make_shared<tcp::socket>(ex);
            *sock = std::move(std::get<1>(v));
            int c = kc_count.load(std::memory_order_relaxed);
            co_spawn(ex, handle_connection(sock, channel, kc, kc_count, *mc, *rc, c), asio::detached);
            co_spawn(ex, [](std::shared_ptr<UChannel<std::tuple<std::shared_ptr<UChannel<IdMessage>>, std::tuple<int, std::shared_ptr<UChannel<std::string>>>>>> relay_channel,std::shared_ptr<UChannel<IdMessage>> m,std::shared_ptr<UChannel<std::string>> r,
            int c) -> asio::awaitable<void>
            {
                co_await relay_channel->async_send(boost::system::error_code{}, std::make_tuple(m, std::make_tuple(c, r)), asio::use_awaitable);
                co_return;
            }(relay_channel, mc, rc, c), asio::detached);
            kc_count += 1;
        }
        else
        {
            std::cout << "breaking now...\n";
            break;
        }
    }

    co_await kill_relay->async_send(boost::system::error_code{}, true, asio::use_awaitable);
    // Use a condition variable instead of whatever this is.
    while(kc_count > 0)
    {
        co_await kc.async_receive(asio::use_awaitable);
    }
    co_return;
}

asio::awaitable<void> Comms::handle_connection(std::shared_ptr<tcp::socket> stream, Channel& kill_accept_loop, Channel& kc, std::atomic<int>& kc_count, UChannel<IdMessage>& msg_channel, UChannel<std::string>& response_channel, int id)
{
    // Re-read this code.
    IdMessage m;
    m.id = id;

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

        std::tie(ec, std::ignore) = co_await stream->async_receive(asio::buffer(buffer_size), tok);
        if (ec) break;

        buffer.assign(*buffer_size, '\0');

        std::tie(ec, n) = co_await stream->async_receive(asio::buffer(buffer), tok);

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
            {
                k = false;
                continue;
            }
            m.message = buffer;
            co_await msg_channel.async_send(boost::system::error_code{}, m, asio::use_awaitable);
        }

        if (ec) break;
    }
    m.message = "cancel";
    co_await msg_channel.async_send(boost::system::error_code{}, m, asio::use_awaitable);
    kc_count -= 1;
    kc.try_send(boost::system::error_code{}, true);
    co_await kill_response.async_send(boost::system::error_code{}, true, asio::use_awaitable);
    co_return;
}

asio::awaitable<void> Comms::respond(std::shared_ptr<tcp::socket> stream, UChannel<std::string>& response_channel, Channel& kill_response)
{
    auto coroutine = [](std::shared_ptr<tcp::socket> stream, UChannel<std::string>& response_channel) -> asio::awaitable<void>
    {
        for(std::string response;;)
        {
            response = co_await response_channel.async_receive(asio::use_awaitable);
            // make this a tuple or something.
            co_await stream->async_send(asio::buffer(std::to_string(response.size())), asio::use_awaitable);
            co_await stream->async_send(asio::buffer(response), asio::use_awaitable);
        }
        co_return;
    };
    co_await (coroutine(stream, response_channel) ||
              kill_response.async_receive(asio::use_awaitable));
    co_return;
}