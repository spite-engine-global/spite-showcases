#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

 
 
namespace warzone
{
    struct Heightfield
    {
        int n = 0;             
        float gridM = 2.0f;    
        float half = 0.0f;     
        std::vector<float> heights;  

        bool valid() const { return n > 1 && heights.size() == static_cast<size_t>(n) * n; }

        bool load(const std::filesystem::path& meta, const std::filesystem::path& data, std::string& error)
        {
            std::ifstream m(meta);
            if (!(m >> n >> gridM >> half))
            {
                error = "cannot read " + meta.string();
                return false;
            }
            std::ifstream d(data, std::ios::binary);
            heights.assign(static_cast<size_t>(n) * n, 0.0f);
            if (!d.read(reinterpret_cast<char*>(heights.data()), static_cast<std::streamsize>(heights.size() * 4)))
            {
                error = "cannot read " + data.string();
                heights.clear();
                return false;
            }
            return true;
        }

        float at(int ix, int iz) const
        {
            ix = std::clamp(ix, 0, n - 1);
            iz = std::clamp(iz, 0, n - 1);
            return heights[static_cast<size_t>(iz) * n + ix];
        }

         
        float height(float x, float z) const
        {
            if (!valid())
                return 0.0f;
            const float gx = std::clamp((x + half) / gridM, 0.0f, static_cast<float>(n - 1));
            const float gz = std::clamp((z + half) / gridM, 0.0f, static_cast<float>(n - 1));
            const int ix = std::min(static_cast<int>(gx), n - 2);
            const int iz = std::min(static_cast<int>(gz), n - 2);
            const float fx = gx - ix, fz = gz - iz;
            const float h0 = at(ix, iz) * (1 - fx) + at(ix + 1, iz) * fx;
            const float h1 = at(ix, iz + 1) * (1 - fx) + at(ix + 1, iz + 1) * fx;
            return h0 * (1 - fz) + h1 * fz;
        }

         
        void gradient(float x, float z, float& dx, float& dz) const
        {
            const float e = gridM;
            dx = (height(x + e, z) - height(x - e, z)) / (2 * e);
            dz = (height(x, z + e) - height(x, z - e)) / (2 * e);
        }

         
        bool raycast(const float origin[3], const float dir[3], float maxDist, float& hitDist) const
        {
            if (!valid())
                return false;
            const float step = gridM * 0.5f;
            float prev = 0.0f;
            float prevAbove = origin[1] - height(origin[0], origin[2]);
            if (prevAbove < 0)
            {
                hitDist = 0;
                return true;
            }
            for (float t = step; t <= maxDist; t += step)
            {
                const float px = origin[0] + dir[0] * t, py = origin[1] + dir[1] * t, pz = origin[2] + dir[2] * t;
                const float above = py - height(px, pz);
                if (above < 0)
                {
                    float lo = prev, hi = t;
                    for (int i = 0; i < 12; ++i)
                    {
                        const float mid = 0.5f * (lo + hi);
                        const float a = origin[1] + dir[1] * mid - height(origin[0] + dir[0] * mid, origin[2] + dir[2] * mid);
                        (a < 0 ? hi : lo) = mid;
                    }
                    hitDist = hi;
                    return true;
                }
                prev = t;
                prevAbove = above;
            }
            return false;
        }
    };
}  
