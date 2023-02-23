#ifndef CACHIER_CONTROLLER
#define CACHIER_CONTROLLER

#include <boost/asio/io_context.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/co_spawn.hpp>
#include <memory>
#include <string>
#include <utility>
#include "storage.hpp"

namespace asio = boost::asio;
using boost::system::error_code;
using Executor = asio::any_io_executor;
using Channel = asio::use_awaitable_t<Executor>::as_default_on_t<
    asio::experimental::concurrent_channel<void(error_code, std::string)>>;
using asio::ip::tcp;

template <typename T, typename U>
class Controller
{
public:

    Controller(std::shared_ptr<asio::io_context> io_ctx, Storage<T, U> storage, std::shared_ptr<Channel> cancellation_channel);

private:
    Storage<T, U> _storage;
    asio::awaitable<void> accept_loop(std::shared_ptr<asio::io_context> io_ctx, std::shared_ptr<Channel> channel);
    asio::awaitable<void> handle_connection(tcp::socket&& stream, std::shared_ptr<Channel> channel);
};

#include "controller.inl"

#endif