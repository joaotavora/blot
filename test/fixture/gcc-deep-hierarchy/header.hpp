#pragma once

// Outer header.  Does NOT include inner/header.hpp — these are two independent
// headers that happen to share the same filename at different directory levels.
inline int outer_fn() { return 1; }
