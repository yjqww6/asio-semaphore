#pragma once
#include "details/nullary_op.hpp"
#include <mutex>
#include <atomic>
#include <boost/asio/async_result.hpp>
#include <boost/asio/execution.hpp>
#include <boost/asio/prefer.hpp>
#include <boost/asio/require.hpp>
#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_executor.hpp>

template <typename Executor, typename Mutex = std::mutex>
class semaphore
{
    Executor ex_;
    [[no_unique_address]] Mutex mutex_;
    std::atomic_size_t n_;
    details::queue<details::nullary_op_base> q_;

    bool try_post_fast_path(std::size_t x)
    {
        auto n = n_.load(std::memory_order_relaxed);
        while(n > 0)
        {
            if(n_.compare_exchange_weak(n, n + x, std::memory_order_release, std::memory_order_relaxed))
            {
                return true;
            }
        }
        return false;
    }
public:
    using executor_type = Executor;
    explicit semaphore(const executor_type& ex, std::size_t n = 0) :
        ex_(ex), n_(n)
    {
    }

    executor_type get_executor() const
    {
        return ex_;
    }

    void post()
    {
        if(try_post_fast_path(1))
        {
            return;
        }

        std::unique_lock lock(mutex_);
        if(auto op = q_.pop())
        {
            lock.unlock();
            op->complete();
        }
        else
        {
            n_.fetch_add(1, std::memory_order_release);
            assert(q_.empty() && n_ == 1);
        }
    }

    void post(std::size_t n)
    {
        if(n == 0)
        {
            return;
        }
        if(try_post_fast_path(n))
        {
            return;
        }
        details::queue<details::nullary_op_base> ops;

        {
            std::lock_guard lock(mutex_);
            while(n > 0)
            {
                auto op = q_.pop();
                if(!op)
                {
                    break;
                }
                --n;
                ops.push(op);
            }
            if(n > 0)
            {
                n_.fetch_add(n, std::memory_order_release);
                assert(q_.empty() || n_ > 0);
            }
        }

        while(auto p = ops.pop())
        {
            p->complete();
        }
    }

    bool try_wait()
    {
        auto n = n_.load(std::memory_order_relaxed);
        while(n > 0)
        {
            if(n_.compare_exchange_weak(n, n - 1, std::memory_order_acquire, std::memory_order_relaxed))
            {
                return true;
            }
        }
        return false;
    }

    // post() synchronized with the invocation of completion_handler or a successful try_wait()
    // try_wait() : ok, post() always increased n_ with release order.
    // completion_handler : fast path is synchronized by try_wait();
    //  slow path is synchronized on mutex_ (q_ is locked).

    template<typename CompletionToken>
    auto async_wait(CompletionToken&& token)
    {
        namespace execution = boost::asio::execution;
        using boost::asio::prefer;
        using boost::asio::require;
        using boost::asio::async_initiate;

        return async_initiate<CompletionToken, void()>(
                    [this](auto&& completion_handler) {
            auto ex = boost::asio::get_associated_executor(completion_handler, ex_);

            if(try_wait())
            {
                execution::execute(require(ex, execution::blocking.never),
                                   std::move(completion_handler));
                return;
            }

            auto ex1 = prefer(ex_, execution::outstanding_work.tracked);
            auto ex2 = prefer(ex, execution::outstanding_work.tracked);
            auto alloc = boost::asio::get_associated_allocator(completion_handler);

            auto op = details::make_nullary_op([ex1, ex2, h = std::move(completion_handler), alloc]
                                               () mutable {
                execution::execute(prefer(require(ex1, execution::blocking.never),
                                          execution::relationship.fork,
                                          execution::allocator(alloc)),
                                   [ex2, h = std::move(h)] () mutable {
                    execution::execute(prefer(ex2, execution::blocking.possibly),
                                       std::move(h));
                });
            }, alloc);

            std::unique_lock lock(mutex_);
            q_.push(op);

            // n_ might become positive during previous steps,
            // but not again during locked.
            details::queue<details::nullary_op_base> ops;

            auto n = n_.load(std::memory_order_relaxed);
            while(n > 0 && !q_.empty())
            {
                if(n_.compare_exchange_weak(n, n - 1, std::memory_order_acquire, std::memory_order_relaxed))
                {
                    auto op = q_.pop();
                    ops.push(op);
                }
            }
            lock.unlock();

            while(auto p = ops.pop())
            {
                p->complete();
            }
        }, token);
    }
};
