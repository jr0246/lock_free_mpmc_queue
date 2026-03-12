# lock_free_mpmc_queue
Lock-free implementation of a multi-producer, multi-consumer queue in C++.

## Key characteristics

* Lock-free, non-blocking, FIFO, multi-producer, multi-consumer queue based on atomic operations.
* Bounded queue size (specified at compile time) - size must be a power of two (2^N).
* Supports trivial and non-trivial data types (with nothrow constructors).
* No memory allocation or deallocation (aside from queue initialisation and destruction).
* Significantly outperforms Boost's lock-free queue (see benchmark tests below).

## Implementation overview

* Ensures push/pop remain lock-free by allocating the queue's memory during initialisation
* Uses bitwise AND with a bitmask to index the entries of the queue (rather than expensive modulo/division arithmetic)
* Uses a separate read (pop) and write (push) index for the producers and consumers.
* Producers never access the read index and consumers never access the write index.
* Atomic compare-and-swap operations are used to signal whether a thread has the right to modify a given entry in the 
  queue.
* Atomic operations on the read and write indices currently use sequentially-consistent memory ordering (but there may be scope for fine-tuning in a future update).
* Atomic operations to load/update the sequence number of an entry use finer-grained acquire and release memory ordering
  - using these rather than sequentially-consistent ordering resulted in a measurable improvement in the benchmark tests (bandwidth of 56m vs. 36m for 4-byte/32-bit data types).

## API and Usage
- `template <typename value_type, std::size_t N, typename Alloc> lock_free_mpmc_queue(const Alloc& alloc)`

  Construct a queue with data type `value_type`, size `N`, and a custom allocator, `Alloc`. 
  Default allocator is `std::allocator<value_type>`.

- `template <typename... Args> constexpr bool emplace(Args&&... args) noexcept`

  Enqueues an element with construction in-place. Requires `value_type` to be nothrow constructible with arguments `args`.

  **Returns:** `true` if the item was enqueued and `false` otherwise.

- `template <typename U> requires std::is_nothrow_constructible_v<value_type, U&&> constexpr bool push(U&& d) noexcept`

  Enqueues an element using perfect forwarding of `U&&` to the appropriate constructor of `value_type`. Element will be
  constructed in-place. Requires `value_type` be nothrow constructible from `U&&`.

  **Returns:** `true` if the item was enqueued and `false` otherwise.

- `template<typename U> requires std::is_same_v<U, value_type&> && (std::is_nothrow_move_assignable_v<value_type> || std::is_nothrow_copy_assignable_v<value_type>) constexpr bool pop(U&& d) noexcept`

  Pops the head of the queue and assigns the value of head into parameter `d` either via move or copy assignment.
  The destructor of head is then run. `U` must be the same as `value_type&` and either nothrow move or copy constructible.
  
  **Returns:** `true` if the item was dequeued and `false` otherwise.

- `template <typename U> requires std::is_nothrow_constructible_v<value_type, U&&> constexpr bool push_keep_n(U&& d) noexcept`

  Enqueues an element, evicting the head of the queue if this operation would cause the queue to hold more than its 
  maximum number of elements.

  **Returns:** `true` when the item is enqueued.

- `template<typename U> requires std::is_same_v<U, value_type&> && std::is_nothrow_copy_assignable_v<value_type> constexpr bool peek(U&& d) noexcept`

  Copies the value of the head of the element into `d`. `value_type` must be nothrow copy assignable.
  
  **Returns:** `true` if the queue is not empty and `false` otherwise.

- `template <typename F, typename U> requires std::predicate<F, value_type> && 
    std::is_same_v<U, value_type&> && (std::is_nothrow_move_assignable_v<value_type> || std::is_nothrow_copy_assignable_v<value_type>)
  constexpr bool pop_if(F&& f, U&& d) noexcept`

  Pops the head of the queue if the value satisfies the predicate `F` and assigns the value to `d`.

  **Returns:** `true` if the pop was successful and `false` otherwise.

- `template <typename F> requires std::predicate<F, value_type&> constexpr size_t consume(F&& f)`

  Pops the head of the queue until the popped value satisfies the predicate `F`, or the queue is empty.

  **Returns:** the number of elements popped from the queue.

- `constexpr bool empty() noexcept`

  **Returns:** `true` if the queue has no elements.

- `constexpr size_t size() const noexcept`

  **Returns:** the number of elements in the queue.

- `constexpr size_t capacity() const noexcept`

  **Returns:** the total capacity of the queue.

## Basic tests
Acceptance tests, including basic concurrent producer/consumer scenarios, can be found in `test/lock_free_queue_test.hpp`.

## Benchmarking
The Strauss MPMC Queue (referenced below) has a useful benchmarking tool. It has been imported into this project for convenience,
and can be run using `make report`. The report will be exported as a `.txt` file, and it can be parsed as the excerpt below by running
`./scripts/report-processing.pl PATH_TO_REPORT_TXT`.

Sample output when run on a 6-core Intel(R) Core(TM) i7-8750H CPU @ 2.20GHz machine running Ubuntu 24.04 shows the queue
outperforming Boost's lock-free queue by multiple times (testing 4-byte and 8-byte data types)
(excerpt below from `reports/q-bw-report.sample.txt`):

```
report for data size: 4
fastest 1-to-1:  data_sz: 4 index_sz: 8 queue_name: mpmc_queue<ff> capacity: 64 bandwidth: 54,799,836
fastest 2-to-2:  data_sz: 4 index_sz: 8 queue_name: mpmc_queue<ff> capacity: 64 bandwidth: 14,955,060 (27.29)
fastest 1-to-2:  data_sz: 4 index_sz: 8 queue_name: mpmc_queue<ff> capacity: 64 bandwidth: 15,885,285 (28.99)
fastest 2-to-1:  data_sz: 4 index_sz: 8 queue_name: mpmc_queue<ff> capacity: 64 bandwidth: 15,854,610 (28.93)
boostq  1-to-1:  data_sz: 4 index_sz: - queue_name: boost:lf:queue capacity: 64 bandwidth: 5,978,466 (10.91)
boostq  2-to-2:  data_sz: 4 index_sz: - queue_name: boost:lf:queue capacity: 64 bandwidth: 5,271,384 (9.62)
report for data size: 8
fastest 1-to-1:  data_sz: 8 index_sz: 8 queue_name: mpmc_queue<ff> capacity: 64 bandwidth: 54,303,810
fastest 2-to-2:  data_sz: 8 index_sz: 8 queue_name: mpmc_queue<ff> capacity: 64 bandwidth: 13,759,095 (25.34)
fastest 1-to-2:  data_sz: 8 index_sz: 8 queue_name: mpmc_queue<ff> capacity: 64 bandwidth: 15,195,217 (27.98)
fastest 2-to-1:  data_sz: 8 index_sz: 8 queue_name: mpmc_queue<ff> capacity: 64 bandwidth: 15,287,440 (28.15)
boostq  1-to-1:  data_sz: 8 index_sz: - queue_name: boost:lf:queue capacity: 64 bandwidth: 5,134,854 (9.46)
boostq  2-to-2:  data_sz: 8 index_sz: - queue_name: boost:lf:queue capacity: 64 bandwidth: 4,866,479 (8.96)
```

## Acknowledgements/Credits
This queue implementation is an extension of approaches taken by the following three queues:
1) MPMC Queue (presented at CppCon 2023) - Erez Strauss: http://github.com/erez-strauss/lockfree_mpmc_queue/
2) Single-producer, single-consumer queue (presented at CppCon 2023) - Charles Frasch: https://github.com/CharlesFrasch/cppcon2023
3) MPMCQueue - Erik Rigtorp: (https://github.com/rigtorp/MPMCQueue)

`lock_free_mpmc_queue` mostly builds upon the API of Queue #1. However, while Queue #1 is restricted to use with
trivial types only, `lock_free_mpmc_queue` extends the functionality to work with non-trivial types `T`
(provided the constructors and assignment operators of `T` are `nothrow`). `lock_free_mpmc_queue` has also had allocator aware support added.

In addition, the API of the new queue takes advantage of many modern C++ features, such as extensive use of concepts,
as well as extra guarantees when using `constexpr` by default on method signatures.
