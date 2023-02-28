#include "storage.hpp"
#include "comms.hpp"
#include <string>

struct key
{
public:
    int id;
    float date;

    friend bool operator==(const key& lhs, const key& rhs)
    {
        if(lhs.id == rhs.id && lhs.date == rhs.date)
            return true;
        return false;
    }
};

namespace std
{
    template<>
    struct hash<key>
    {
        std::size_t operator()(const key& k) const
        {
            std::hash<string> hasher;
            return (hasher(std::to_string(k.id)) ^
                    (hasher(std::to_string(k.date)) << 1));
        }
    };
}

int main()
{
    asio::io_context io_context(2);
    Channel cancellation_channel(io_context, 1);
    UChannel<std::string> msg_channel(io_context, 10);
    UChannel<std::string> response_channel(io_context, 10);
    Storage<std::string, key> storage(io_context, 5, msg_channel, response_channel);
    Comms command_receiver(io_context, cancellation_channel, 8001, msg_channel, response_channel);

    // test: get and insert.
    co_spawn(io_context, [](Storage<std::string, key>& storage) -> asio::awaitable<void>
    {
        std::vector<std::tuple<key, std::string>> data;
        key k;
        k.id = 5;
        k.date = 2.5;
        std::string value = "the cache is working!";
        data.push_back(std::make_tuple(k, value));

        co_await storage.insert(data);
        std::optional<std::string> result = co_await storage.get(k);
        if(result.has_value())
            std::cout << *result << "\n";
        else
            std::cout << "doesn't exist\n";

        k.id = 6;
        result = co_await storage.get(k);
        if(result.has_value())
        {
            std::cout << *result << "\n";
        }
        else
        {
            std::cout << "entry does not exist in cache...\n";
        }
        co_return;
    }(storage), asio::detached);

    
    io_context.run();
}