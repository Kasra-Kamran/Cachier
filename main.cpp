#include "storage.hpp"
#include "controller.hpp"
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
    auto io_context = std::make_shared<boost::asio::io_context>(2);
    auto channel = make_shared<Channel>(*io_context, sizeof(std::string));
    Storage<int, key> storage(io_context, 5);
    Controller<int, key> remote(io_context, std::move(storage), channel);

    key k;
    k.id = 5;
    k.date = 2.5;

    co_spawn(*io_context, storage.get(k), boost::asio::detached);
    
    io_context->run();
}