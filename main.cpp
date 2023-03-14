#include "storage.hpp"
#include "comms.hpp"
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

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

    static key from_json(json j)
    {
        try
        {
            key k;
            k.id = j["id"];
            k.date = j["date"];
            return k;
        }
        catch(const std::exception& e)
        {
            std::cout << __LINE__ << " : " << e.what() << "\n";
            throw;
        }
    }

    static json to_json(key k)
    {
        return json
        {
            {"id", k.id},
            {"date", k.date},
        };
    }
};

struct value
{
public:
    std::string data;

    friend bool operator==(const value& lhs, const value& rhs)
    {
        if(lhs.data == rhs.data)
            return true;
        return false;
    }

    static value from_json(json j)
    {
        try
        {
            value v;
            v.data = j["data"];
            return v;
        }
        catch(const std::exception& e)
        {
            std::cout << __LINE__ << " : " << e.what() << "\n";
            throw;
        }
    }

    static json to_json(value v)
    {
        return json
        {
            {"data", v.data},
        };
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

    template<>
    struct hash<value>
    {
        std::size_t operator()(const value& v) const
        {
            return (std::hash<string>()(v.data));
        }
    };
}

int main()
{
    asio::io_context io_context(2);
    Channel command_receiver_cancellation_channel(io_context, 2);
    UChannel<IdMessage> msg_channel(io_context, 10);
    UChannel<IdMessage> response_channel(io_context, 10);
    Storage<value, key> storage(io_context, 5, msg_channel, response_channel);
    Comms command_receiver(io_context, command_receiver_cancellation_channel, 8001, msg_channel, response_channel);
    // Comms main(io_context, cancellation_channel)

    // test: get and insert.
    co_spawn(io_context, [](Storage<value, key>& storage) -> asio::awaitable<void>
    {
        std::vector<std::tuple<key, value>> data;
        key k;
        k.id = 5;
        k.date = 2.5;
        value v;
        v.data = "the cache is working!";
        data.push_back(std::make_tuple(k, v));

        co_await storage.insert(data);
        std::optional<value> result = co_await storage.get(k);
        if(result.has_value())
            std::cout << value::to_json(*result).dump() << "\n";
        else
            std::cout << "doesn't exist\n";

        k.id = 6;
        result = co_await storage.get(k);
        if(result.has_value())
        {
            std::cout << value::to_json(*result).dump() << "\n";
        }
        else
        {
            std::cout << "entry does not exist in cache...\n";
        }
        co_return;
    }(storage), asio::detached);

    
    io_context.run();
}