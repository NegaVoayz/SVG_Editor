#include "History.hh"

#include <atomic>
#include <stdexcept>

namespace {

std::atomic<GlobalCommitId> next_commit{0};

}  // namespace

GlobalCommitId next_global_commit_id() {
    const GlobalCommitId id = next_commit.fetch_add(1, std::memory_order_relaxed) + 1;
    if (id == 0) throw std::overflow_error("the global commit id space is exhausted");
    return id;
}
