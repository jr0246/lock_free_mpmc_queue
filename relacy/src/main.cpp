#include <relacy/context.hpp>

#include "relacy_lock_free_mpmc_queue.hpp"

int main() {
    rl::simulate<jr::lock_free_mpmc_queue<int, 256UL>>();
}
