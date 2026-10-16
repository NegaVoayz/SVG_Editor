#pragma once

#include <string>

// The result of an operation that can fail for a reason the user has to be told
// about. The message is meant to be shown, not parsed.
struct Status {
    bool ok = true;
    std::string message;
};
