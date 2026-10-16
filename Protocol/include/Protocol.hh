#pragma once

// The protocol, by the name every layer above Model knows it by. This header holds no
// definition of its own: the protocol is the four things next to it, and a file that wants
// one of them may include that one instead.
//
//   NodeRef.hh  how an instruction names a node, and what that name turns out to be
//   Intent.hh   what a front end can ask for, and the idiom every layer that answers an
//               intent is built with
//   View.hh     what a read hands back
//   Outcome.hh  what comes back out, whichever of the three branches answered
//
// The order is the order they depend on each other, which is why there is no loop in the
// include graph and why nothing here has to know about anything above it.

#include "Intent.hh"
#include "NodeRef.hh"
#include "Outcome.hh"
#include "View.hh"
