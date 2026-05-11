#ifndef MPMC_QUEUE_LOCK_FREE_MPMC_QUEUE_H
#define MPMC_QUEUE_LOCK_FREE_MPMC_QUEUE_H
#include <atomic>
#include <climits>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace jr {
#ifdef __cpp_lib_hardware_interference_size
    static constexpr size_t cache_line_size =
        std::hardware_destructive_interference_size;
#else
    static constexpr size_t cache_line_size = 64;
#endif

    template <typename T>
    struct entry {
        using index_type = uint64_t;

        alignas(cache_line_size) std::atomic<size_t> seq{0};
        alignas(T) char buf[sizeof(T)]{};

        constexpr ~entry() noexcept {
            if (seq & 1U) destruct();
        }

        template <typename... Args>
            requires std::is_nothrow_constructible_v<T, Args&&...>
        constexpr void construct(Args&&... args) noexcept {
            new(&buf) T(std::forward<Args>(args)...);
        }

        constexpr void destruct() noexcept {
            reinterpret_cast<T*>(&buf)->~T();
        }

        constexpr T&& move() noexcept {
            return reinterpret_cast<T&&>(buf);
        }

        constexpr void set_seq(const index_type s) noexcept {
            seq.store(s, std::memory_order_release);
        }

        [[nodiscard]] constexpr index_type get_seq() const noexcept { return seq.load(std::memory_order_acquire); }

        [[nodiscard]] constexpr const T* get_data() const noexcept { return reinterpret_cast<const T*>(&buf); }
    };


    template <typename Data, std::size_t N, typename Alloc = std::allocator<entry<Data>>>
        requires std::is_nothrow_destructible_v<Data>
    class lock_free_mpmc_queue : Alloc {
    public:
        // 64-bit integers can be used as indices std::atomic<std::uint64_t> is lock-free on x86
        using index_type = uint64_t;
        using value_type = Data;
        using allocator_traits = std::allocator_traits<Alloc>;
        using size_type = allocator_traits::size_type;
        using cursor_type = std::atomic<size_type>;

        static constexpr unsigned bits_in_index() { return sizeof(index_type) * CHAR_BIT; }

        static constexpr unsigned bits_for_value(unsigned n) {
            unsigned b{0};
            while (n != 0) {
                b++;
                n >>= 1U;
            }
            return b;
        }

        static_assert(cursor_type::is_always_lock_free, "the type used for indexing the queue must be lock-free.");
        static_assert(N == 0 || bits_in_index() > bits_for_value(N),
                      "type used for enqueue and dequeue indices must be larger than the number of elements that will be held at once (ideally, much larger).")
        ;
        static_assert(std::has_single_bit(N), "size of queue must be a power of two: 1, 2, 4, 8, 16 ...");

        constexpr explicit lock_free_mpmc_queue(const Alloc& alloc = Alloc{}) : _capacity{N},
            _mask{_capacity - 1}, _ring{allocator_traits::allocate(*this, _capacity)}, _allocator{alloc},
            _write_index{0}, _read_index{0} {
            for (index_type i = 0; i < _capacity; ++i) _ring[i].set_seq(i << 1);
        }

        ~lock_free_mpmc_queue() noexcept {
            for (index_type i = 0; i < _capacity; ++i) {
                _ring[i].~entry();
            }
            _allocator.deallocate(_ring, _capacity);
        }

        lock_free_mpmc_queue(const lock_free_mpmc_queue&) = delete;
        lock_free_mpmc_queue(lock_free_mpmc_queue&&) = delete;
        lock_free_mpmc_queue& operator=(const lock_free_mpmc_queue&) = delete;
        lock_free_mpmc_queue& operator=(lock_free_mpmc_queue&&) = delete;

        template <typename... Args>
            requires std::is_nothrow_constructible_v<Data, Args&&...>
        [[using gnu: hot, flatten]] constexpr bool emplace(Args&&... args) noexcept {
            while (true) {
                index_type wr_index = _write_index.load();
                auto entry = element(wr_index);

                if (const index_type seq = entry->get_seq(); seq == wr_index << 1) {
                    if (_write_index.compare_exchange_strong(wr_index, wr_index + 1)) {
                        entry->construct(std::forward<Args>(args)...);
                        entry->set_seq(wr_index << 1 | 1U);
                        return true;
                    }
                }
                else if (seq == (wr_index << 1 | 1U) || seq == (wr_index + _capacity) << 1) {
                    _write_index.compare_exchange_strong(wr_index, wr_index + 1);
                }
                else if (seq + (_capacity << 1) == (wr_index << 1 | 1U)) {
                    return false;
                }
            }
        }

        template <typename U>
            requires std::is_nothrow_constructible_v<value_type, U&&>
        [[using gnu: hot, flatten]] constexpr bool push(U&& d) noexcept {
            return emplace(std::forward<U>(d));
        }

        template<typename U>
        requires std::is_same_v<U, value_type&> &&
            (std::is_nothrow_move_assignable_v<value_type> || std::is_nothrow_copy_assignable_v<value_type>)
        [[using gnu: hot, flatten]] constexpr bool pop(U&& d) noexcept {
            index_type rd_index = _read_index.load();

            while (true) {
                auto entry = element(rd_index);
                auto seq = entry->get_seq();
                if (seq == (rd_index << 1 | 1U)) {
                    if (_read_index.compare_exchange_strong(rd_index, rd_index + 1)) {
                        d = entry->move();
                        entry->destruct();
                        entry->set_seq((rd_index + _capacity) << 1U);
                        return true;
                    }
                }
                else if ((seq | 1U) == ((rd_index + _capacity) << 1 | 1U)) {
                    _read_index.compare_exchange_strong(rd_index, rd_index + 1);
                }
                else if (seq == rd_index << 1) {
                    return false;
                }
                rd_index = _read_index.load();
            }
        }

        template <typename U>
            requires std::is_nothrow_constructible_v<value_type, U&&>
        [[using gnu: hot, flatten]] constexpr bool push_keep_n(U&& d) noexcept
        {
            while (true)
            {
                if (push(d)) return true;
                value_type lost;
                pop(lost);
            }
        }

        template<typename U>
        requires std::is_same_v<U, value_type&> && std::is_nothrow_copy_assignable_v<value_type>
        [[using gnu: hot]] constexpr bool peek(U&& d) noexcept {
            while (true) {
                index_type rd_index = _read_index.load();
                auto entry = element(rd_index);

                if (entry->get_seq() == rd_index << 1) {
                    return false;
                }

                if (entry->get_seq() == (rd_index << 1 | 1)) {
                    d = *entry->get_data();
                    return true;
                }
                if (entry->get_seq() >> 1 == rd_index + _capacity) {
                    _read_index.compare_exchange_strong(rd_index, rd_index + 1);
                }
            }
        }

        template <typename F, typename U>
            requires std::predicate<F, value_type&> &&
                std::is_same_v<U, value_type&> &&
                    (std::is_nothrow_move_assignable_v<value_type> || std::is_nothrow_copy_assignable_v<value_type>)
        [[using gnu: hot, flatten]] constexpr bool pop_if(F&& f, U&& d) noexcept {
            while (true) {
                auto rd_index = _read_index.load();
                auto entry = element(rd_index);
                if (entry->get_seq() == rd_index << 1) {
                    return false;
                }
                if ((rd_index << 1 | 1U) == entry->get_seq()) {
                    if (!f(*entry->get_data())) {
                        return false;
                    }
                    if (_read_index.compare_exchange_strong(rd_index, rd_index + 1)) {
                        d = entry->move();
                        entry->destruct();
                        entry->set_seq((rd_index + _capacity) << 1U);
                        return true;
                    }
                }
                else if (entry->get_seq() >> 1 == rd_index + _capacity) {
                    _read_index.compare_exchange_strong(rd_index, rd_index + 1);
                }
            }
        }

        template <typename F>
            requires std::predicate<F, value_type&>
        [[using gnu: hot, flatten]] constexpr size_t consume(F&& f) {
            size_t r{0};
            value_type v;
            while (pop(v)) {
                ++r;
                if (f(v)) return r;
            }
            return r;
        }

        [[using gnu: hot, flatten]] constexpr bool empty() noexcept {
            index_type rd_index = _read_index.load();

            if (auto entry = element(rd_index); entry->get_seq() == rd_index << 1) return true;
            return false;
        }

        [[using gnu: hot, flatten]] [[nodiscard]] constexpr bool empty() const noexcept {
            index_type rd_index = _read_index.load();

            if (auto entry = element(rd_index); entry->get_seq() == rd_index << 1) return true;
            return false;
        }

        [[using gnu: hot, flatten]] [[nodiscard]] constexpr size_t size() const noexcept {
            if (empty()) return 0;
            if (_write_index >= _read_index) return _write_index - _read_index;
            return _write_index + _capacity - _read_index;
        }

        [[nodiscard]] constexpr size_t capacity() const noexcept { return _capacity; }

    private:
        const size_t _capacity;
        const size_type _mask;
        entry<Data>* _ring;
        Alloc _allocator;

        alignas(cache_line_size) std::atomic<size_type> _write_index;
        alignas(cache_line_size) std::atomic<size_type> _read_index;

        constexpr auto element(size_type cursor) const noexcept {
            return &_ring[cursor & _mask];
        }
    };
};
#endif //MPMC_QUEUE_LOCK_FREE_MPMC_QUEUE_H
