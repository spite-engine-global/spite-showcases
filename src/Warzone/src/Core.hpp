 
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace warzone
{
    struct V3
    {
        float x = 0, y = 0, z = 0;
        V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
        V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
        V3 operator*(float s) const { return {x * s, y * s, z * s}; }
        V3& operator+=(const V3& o) { return *this = *this + o; }
    };
    inline float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline float len(const V3& a) { return std::sqrt(dot(a, a)); }
    inline V3 norm(const V3& a)
    {
        const float l = len(a);
        return l > 1e-6f ? a * (1.0f / l) : V3{0, 1, 0};
    }

    struct Box
    {
        V3 lo, hi;
        V3 centre() const { return (lo + hi) * 0.5f; }
        V3 size() const { return hi - lo; }
        float volume() const
        {
            const V3 s = size();
            return s.x * s.y * s.z;
        }
    };
    inline float distance(const Box& b, const V3& p)
    {
        const float dx = std::max({b.lo.x - p.x, 0.0f, p.x - b.hi.x});
        const float dy = std::max({b.lo.y - p.y, 0.0f, p.y - b.hi.y});
        const float dz = std::max({b.lo.z - p.z, 0.0f, p.z - b.hi.z});
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    inline bool overlaps(const Box& a, const Box& b, float tol)
    {
        return a.lo.x < b.hi.x + tol && b.lo.x < a.hi.x + tol && a.lo.y < b.hi.y + tol && b.lo.y < a.hi.y + tol &&
               a.lo.z < b.hi.z + tol && b.lo.z < a.hi.z + tol;
    }
    inline float footprintOverlap(const Box& a, const Box& b)
    {
        const float ox = std::min(a.hi.x, b.hi.x) - std::max(a.lo.x, b.lo.x);
        const float oz = std::min(a.hi.z, b.hi.z) - std::max(a.lo.z, b.lo.z);
        return ox > 0 && oz > 0 ? ox * oz : 0.0f;
    }

     
    struct Rng
    {
        uint64_t s;
        explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
        uint64_t next()
        {
            uint64_t z = (s += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }
        float f01() { return static_cast<float>(next() >> 40) / static_cast<float>(1ull << 24); }
        float range(float a, float b) { return a + (b - a) * f01(); }
        int irange(int lo, int hiInclusive) { return lo + static_cast<int>(next() % static_cast<uint64_t>(hiInclusive - lo + 1)); }
        bool chance(float p) { return f01() < p; }
    };
    inline uint32_t hash32(uint32_t x)
    {
        x ^= x >> 16;
        x *= 0x7feb352d;
        x ^= x >> 15;
        x *= 0x846ca68b;
        x ^= x >> 16;
        return x;
    }
}  
