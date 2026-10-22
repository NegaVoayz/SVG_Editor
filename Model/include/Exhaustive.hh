#pragma once

// A dependent false for the fallback branch of an exhaustive variant visitor. It is
// evaluated only when a new alternative reaches a dispatcher with no handler.
template <class...>
inline constexpr bool always_false = false;
