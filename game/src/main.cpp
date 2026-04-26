// Demo dispatcher. Each demo lives in its own TU (WorldDemo.cpp,
// PhysicsDemo.cpp) and exposes a single Run* entry point. This file picks one
// at compile time so we can keep multiple demos around without colliding on
// `int main`.
//
// Default = the active 22A physics-debug demo. Define ARCBIT_DEMO_WORLD when
// invoking CMake (or here) to launch the original world demo instead.

#include <cstdlib>

void RunWorldDemo();
void RunPhysicsDemo();


// #define ARCBIT_DEMO_WORLD

int main(int /*argc*/, char* /*argv*/[])
{
#if defined(ARCBIT_DEMO_WORLD)
    RunWorldDemo();
#else
    RunPhysicsDemo();
#endif
    return EXIT_SUCCESS;
}
