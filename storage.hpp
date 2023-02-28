#ifndef CACHIER_STORAGE
#define CACHIER_STORAGE

#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio.hpp>
#include <memory>
#include <unordered_map>
#include <optional>
#include <vector>
#include <tuple>

namespace asio = boost::asio;
using boost::system::error_code;
using Executor = asio::any_io_executor;
template <typename U>
using UChannel = boost::asio::use_awaitable_t<Executor>::as_default_on_t<
    asio::experimental::concurrent_channel<void(error_code, U)>>;

enum Command
{
    Insert,
    Get,
    Die,
    Ping,
};

template <typename T, typename U>
struct Message
{
public:
    std::optional<U> key;
    std::optional<T> value;
    std::optional<T*>* return_value;
    std::optional<std::vector<std::tuple<U, T>>> data;
    Command command;
};

template <typename T, typename U>
class Storage
{
public:
    Storage(asio::io_context& io_context, std::size_t num_caches, UChannel<std::string>& msg_channel, UChannel<std::string>& response_channel);
    Storage(Storage&& s);
    // One public method for each command.
    asio::awaitable<std::optional<T>> get(U key);
    asio::awaitable<void> insert(std::vector<std::tuple<U, T>> entries);
    asio::awaitable<void> kill();

private:
    std::vector<std::unordered_map<U, T>> _warehouse;
    std::vector<std::shared_ptr<UChannel<Message<T, U>>>> _outgoing;
    std::hash<U> hasher;

    // asio::awaitable<void> cache(std::unordered_map<U, T> box, std::shared_ptr<UChannel<U>> incoming);
};

template <typename T, typename U>
boost::asio::awaitable<void> cache(std::unordered_map<U, T>&& box, UChannel<Message<T, U>>& incoming);

template <typename T, typename U>
asio::awaitable<void> handle_storage_commands(UChannel<std::string>& msg_channel, UChannel<std::string>& response_channel, Storage<T, U>& storage);

#include "storage.inl"

#endif
