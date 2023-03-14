#ifndef CACHIER_COMMS
#define CACHIER_COMMS

#include <boost/asio/io_context.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/co_spawn.hpp>
#include <memory>
#include <string>
#include <utility>
#include <functional>
#include <mutex>
#include <queue>
#include <tuple>
#include "storage.hpp"

namespace asio = boost::asio;
using boost::system::error_code;
using Executor = asio::any_io_executor;
using Channel = asio::use_awaitable_t<Executor>::as_default_on_t<
    asio::experimental::concurrent_channel<void(error_code, bool)>>;
template <typename U>
using UChannel = boost::asio::use_awaitable_t<Executor>::as_default_on_t<
    asio::experimental::concurrent_channel<void(error_code, U)>>;
using asio::ip::tcp;

class Comms
{
public:

    Comms(asio::io_context& io_ctx, Channel& cancellation_channel, std::size_t port, UChannel<IdMessage>& msg_channel, UChannel<IdMessage>& response_channel);

private:

    asio::awaitable<void> accept_loop(
        Channel& channel,
        std::size_t port,
        std::shared_ptr<UChannel<std::tuple<std::shared_ptr<UChannel<IdMessage>>,
        std::tuple<int, std::shared_ptr<UChannel<std::string>>>>>> relay_channel,
        std::shared_ptr<Channel> kill_relay);
    asio::awaitable<void> relay(
        UChannel<IdMessage>& msg_channel,
        UChannel<IdMessage>& response_channel,
        std::shared_ptr<UChannel<std::tuple<std::shared_ptr<UChannel<IdMessage>>,
        std::tuple<int, std::shared_ptr<UChannel<std::string>>>>>> relay_channel,
        std::shared_ptr<Channel> kill_relay);
    asio::awaitable<void> handle_connection(std::shared_ptr<tcp::socket> stream, Channel& kill_accept_loop, Channel& kc, std::atomic<int>& count, UChannel<IdMessage>& msg_channel, UChannel<std::string>& response_channel, int id);
    asio::awaitable<void> respond(std::shared_ptr<tcp::socket> stream, UChannel<std::string>& response_channel, Channel& kill_response);
};

#include "comms.inl"

#endif