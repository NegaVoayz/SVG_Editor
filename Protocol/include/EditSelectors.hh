#pragma once

// How a `move` or `scale` reaches the values it changes. An omitted selector defaults to
// Param and Cascade; the Editor then checks whether those apply to the node it resolved.
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
    // `restrict` on a container changes the container itself. A container has no params,
    // so the one value it owns is its transform; the descendants keep theirs.
    ContainerTransform,
};
