#ifndef CACHIER_STORAGE
#define CACHIER_STORAGE

#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/io_service.hpp>
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
};

template <typename T, typename U>
struct Message
{
public:
    std::optional<U> key;
    std::optional<T> value;
    std::optional<std::vector<std::tuple<U, T>>> data;
    Command command;
};

template <typename T, typename U>
class Storage
{
public:
    Storage(std::shared_ptr<asio::io_service> io_service, std::size_t num_caches);
    Storage(Storage&& s);
    // One public method for each command.
    asio::awaitable<std::optional<T>> get(U key);
    // these really don't need to be awaitable
    asio::awaitable<void> insert(std::vector<std::tuple<U, T>> entries);
    asio::awaitable<void> kill();

private:
    std::vector<std::unordered_map<U, T>> _warehouse;
    std::vector<std::shared_ptr<UChannel<Message<T, U>>>> _outgoing;

    // asio::awaitable<void> cache(std::unordered_map<U, T> box, std::shared_ptr<UChannel<U>> incoming);
};

#include "storage.inl"

#endif
