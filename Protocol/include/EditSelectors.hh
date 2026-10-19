#pragma once

// How a `move` or `scale` reaches the values it changes. The front end reads the two
// selector words into these values; the Editor resolves Default against the node kind.
enum class EditTarget {
    Default,
    Transform,
    Params,
};

enum class EditScope {
    Default,
    Restrict,
    Cascade,
};

// The resolved operation an Editor event reports. It has no Default: by the time the
// edit commits, the node kind has decided what happened.
enum class EditMode {
    Params,
    Transform,
    Cascade,
};
