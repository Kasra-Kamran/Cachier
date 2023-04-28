#ifndef CACHIER_STORAGE
#define CACHIER_STORAGE

#include <boost/asio/experimental/concurrent_channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <boost/asio.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <unordered_map>
#include <optional>
#include <vector>
#include <tuple>
#include <bitset>
#include <mutex>

namespace asio = boost::asio;
using boost::system::error_code;
using Executor = asio::any_io_executor;
template <typename U>
using UChannel = boost::asio::use_awaitable_t<Executor>::as_default_on_t<
    asio::experimental::concurrent_channel<void(error_code, U)>>;
using json = nlohmann::json;

const json CACHE_MISS = {{"available", false},};
const json CACHE_INVALID = {{"valid", false},};
const json CACHE_INVALID_KEY_OR_VALUE = {{"valid_key_and_value", false},};
const json CACHE_INSERTED = {{"insertion_success", true},};
const json CACHE_UNKNOWN_COMMAND = {{"command", "unknown"},};

enum Command
{
    Insert,
    Get,
    Die,
    Ping,
};

struct IdMessage
{
    int id;
    std::string message;
};

// use headers for each memory section.
template <typename S, size_t T>
class MemPool
{
public:
    MemPool()
    {
        available.set();
        memory = (S*)malloc(sizeof(S)*T);
    }

    S* get()
    {
        for(int i = 0; i < T; i++)
        {
            if(available[i])
            {
                available[i] = false;
                return (memory + i);
            }
        }
    }

    void free(S* mem)
    {
        available[(mem - memory) / sizeof(S)] = true;
    }

private:
    S* memory;
    size_t next = 0;
    std::bitset<T> available;
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
    Storage(asio::io_context& io_context, std::size_t num_caches, UChannel<IdMessage>& msg_channel, UChannel<IdMessage>& response_channel);
    Storage(Storage&& s);
    asio::awaitable<std::optional<T>> get(U key);
    asio::awaitable<void> insert(std::vector<std::tuple<U, T>> entries);
    asio::awaitable<void> kill();

private:
    std::vector<std::unordered_map<U, T>> _warehouse;
    std::vector<std::shared_ptr<UChannel<Message<T, U>>>> _outgoing;
    std::hash<U> hasher;
    MemPool<T, 100> _p;

    asio::awaitable<void> cache(std::unordered_map<U, T>&& box, UChannel<Message<T, U>>& incoming);
    asio::awaitable<void> handle_storage_commands(UChannel<IdMessage>& msg_channel, UChannel<IdMessage>& response_channel);
};

#include "storage.inl"

#endif
