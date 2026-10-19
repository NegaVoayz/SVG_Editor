#include "History.hh"

#include <atomic>
#include <stdexcept>

namespace {

std::atomic<CommitId> next_commit{0};

}  // namespace

CommitId next_commit_id() {
    const CommitId id = next_commit.fetch_add(1, std::memory_order_relaxed) + 1;
    if (id == 0) throw std::overflow_error("the commit id space is exhausted");
    return id;
}

void observe_commit_id(CommitId commit) {
    CommitId seen = next_commit.load(std::memory_order_relaxed);
    while (seen < commit) {
        if (next_commit.compare_exchange_weak(seen, commit, std::memory_order_relaxed)) break;
    }
}
