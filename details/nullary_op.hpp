#pragma once
#include <memory>

namespace details
{

struct op_base
{
    op_base* next_;
};

class queue_base
{
    op_base sentinel_;
    op_base *back_;
public:
    explicit queue_base()
    {
        sentinel_.next_ = nullptr;
        back_ = &sentinel_;
    }

    queue_base(queue_base&& other) noexcept
    {
        sentinel_.next_ = std::exchange(other.sentinel_.next_, nullptr);
        back_ = std::exchange(other.back_, nullptr);
    }

    queue_base& operator=(queue_base&& other) noexcept
    {
        queue_base tmp(std::move(other));
        std::swap(sentinel_.next_, tmp.sentinel_.next_);
        std::swap(back_, tmp.back_);
        return *this;
    }

    bool empty() const noexcept
    {
        return sentinel_.next_ == nullptr;
    }

    op_base* head() const
    {
        return sentinel_.next_;
    }

    void push(op_base* op) noexcept
    {
        back_->next_ = op;
        back_ = op;
    }

    op_base* pop() noexcept
    {
        if(empty())
        {
            return nullptr;
        }
        auto p = std::exchange(sentinel_.next_, sentinel_.next_->next_);
        p->next_ = nullptr;
        return p;
    }

    void splice(queue_base&& q) noexcept
    {
        if(q.empty())
        {
            return;
        }
        back_->next_ = std::exchange(q.sentinel_.next_, nullptr);
        back_ = std::exchange(q.back_, &q.sentinel_);
    }
};

template <typename T>
class queue : protected queue_base
{
public:
    explicit queue() = default;
    queue(queue&&) = default;
    queue& operator=(queue&&) = default;
    ~queue()
    {
        auto d = typename T::deleter();
        for(auto p = head(); p; p = p->next_)
        {
            d(static_cast<T*>(p));
        }
    }

    void push(T* op) noexcept
    {
        queue_base::push(op);
    }

    T* pop() noexcept
    {
        return static_cast<T*>(queue_base::pop());
    }

    void splice(queue&& q) noexcept
    {
        queue_base::splice(q);
    }

    using queue_base::empty;
};

struct nullary_op_base : op_base
{
    void (*func_)(nullary_op_base * self, bool);

    void complete()
    {
        func_(this, true);
    }

    void destroy()
    {
        func_(this, false);
    }

    struct deleter
    {
        void operator()(nullary_op_base *p) const
        {
            p->destroy();
        }
    };
};

template <typename FnOnce, typename Allocator>
struct nullary_op : nullary_op_base
{
    [[no_unique_address]] FnOnce fn_;
    [[no_unique_address]] Allocator alloc_;

    struct deleter
    {
        void operator()(nullary_op* p) const
        {
            using Alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<nullary_op>;
            Alloc alloc(p->alloc_);
            std::allocator_traits<Alloc>::destroy(alloc, p);
            std::allocator_traits<Alloc>::deallocate(alloc, p, 1);
        }
    };

    static void do_complete(nullary_op_base * self, bool call)
    {
        auto p = static_cast<nullary_op*>(self);

        if(call)
        {
            if constexpr(std::is_nothrow_move_constructible_v<FnOnce>)
            {
                FnOnce fn(std::move(p->fn_));
                deleter()(p);
                fn();
            }
            else
            {
                std::unique_ptr<nullary_op, deleter> ptr(p);
                FnOnce fn(std::move(p->fn_));
                ptr.reset();
                fn();
            }
        }
        else
        {
            deleter()(p);
        }
    }

    template <typename Fn>
    explicit nullary_op(Fn&& fn, const Allocator& alloc) : fn_(std::forward<Fn>(fn)), alloc_(alloc)
    {
        next_ = nullptr;
        func_ = &do_complete;
    }
};

template <typename Fn, typename Allocator>
auto make_nullary_op(Fn&& fn, const Allocator& alloc)
{
    using FnOnce = std::decay_t<Fn>;
    using Alloc = typename std::allocator_traits<Allocator>
    ::template rebind_alloc<nullary_op<FnOnce, Allocator>>;

    Alloc a(alloc);
    auto p = std::allocator_traits<Alloc>::allocate(a, 1);
    if constexpr(std::is_nothrow_constructible_v<FnOnce, Fn&&>)
    {
        std::allocator_traits<Alloc>::construct(a, p, std::forward<Fn>(fn), alloc);
    }
    else
    {
        try
        {
            std::allocator_traits<Alloc>::construct(a, p, std::forward<Fn>(fn), alloc);
        }
        catch (...)
        {
            std::allocator_traits<Alloc>::deallocate(a, p, 1);
            throw;
        }
    }
    return p;
}

}
