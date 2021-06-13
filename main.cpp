#include "semaphore.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <iostream>

using boost::asio::awaitable;
using boost::asio::use_awaitable_t;

template <typename Executor>
awaitable<void, Executor> test(std::size_t n)
{
    constexpr use_awaitable_t<Executor> use_awaitable;
    semaphore sema(co_await boost::asio::this_coro::executor);
    auto a = std::chrono::steady_clock::now();
    for(std::size_t i = 0; i < n; ++i)
    {
        sema.post();
    }
    auto b = std::chrono::steady_clock::now();
    for(std::size_t i = 0; i < n; ++i)
    {
        co_await sema.async_wait(use_awaitable);
    }
    auto c = std::chrono::steady_clock::now();

    std::cout << std::chrono::duration<double>(b - a).count() << std::endl;
    std::cout << std::chrono::duration<double>(c - b).count() << std::endl;
}

template <typename Executor, typename... Args>
awaitable<void, Executor> test2(semaphore<Executor, Args...> & sema)
{
    constexpr use_awaitable_t<Executor> use_awaitable;
    co_await sema.async_wait(use_awaitable);
    sema.post(1);
}

int main()
{
    boost::asio::io_context io;
    {
        boost::asio::co_spawn(io, test<decltype(io)::executor_type>(10'000'000), boost::asio::detached);
        io.run();
    }

    {
        semaphore sema(io.get_executor(), 100);
        io.post([&] {
            unsigned n = 1'000'000;
            for(std::size_t i = 0; i < n; ++i)
            {
                boost::asio::co_spawn(io, test2(sema), boost::asio::detached);
            }
        });
        io.restart();
        auto a = std::chrono::steady_clock::now();
        io.run();
        auto b = std::chrono::steady_clock::now();
        std::cout << std::chrono::duration<double>(b - a).count() << std::endl;
    }
}
