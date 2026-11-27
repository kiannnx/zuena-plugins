#include "OpticalEngine.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <new>

namespace
{
thread_local bool measuring = false;
thread_local std::size_t allocations = 0;
} // namespace
void *operator new(std::size_t n)
{
    if (measuring)
        ++allocations;
    if (auto *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc{};
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}
void *operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete[](void *p) noexcept
{
    std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
    std::free(p);
}

TEST_CASE("El motor procesa y automatiza el sidechain sin reservar memoria")
{
    luma::Controls c;
    luma::OpticalEngine e;
    e.prepare(96000, c);
    std::array<float, 513> l{}, r{}, key{};
    key.fill(0.30f);
    allocations = 0;
    measuring = true;
    for (int n = 0; n < 100; ++n)
    {
        c.limit = n % 2 != 0;
        c.external = n % 3 != 0;
        c.emphasisDb = n % 10;
        c.highpassHz = (n * 3) % 301;
        c.attackMs = 1 + (n % 40);
        c.stereoLinkPercent = n;
        c.rangeDb = n % 41;
        c.audition = n % 2 != 0;
        c.precision = n % 3 == 0;
        e.setControls(c);
        e.process(l, r, key);
    }
    measuring = false;
    CHECK(allocations == 0);
}
