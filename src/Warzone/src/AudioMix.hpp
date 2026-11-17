#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace warzone::audio
{
    enum class Category { Weapons, Explosions, Vehicles, Impacts };
    struct Settings
    {
        float master = 100, mute = 0, weapons = 100, explosions = 100, vehicles = 100, impacts = 100;
        float blastMinimum = 0, blastThreshold = 10, falloff = 1, delay = 1;
    };
    struct Field
    {
        const char* id;
        const char* label;
        float Settings::*member;
        float min, max, step;
    };
    inline constexpr std::array<Field, 10> FIELDS = {{
        {"audioMaster", "Master volume (%)", &Settings::master, 0, 100, 1},
        {"audioMute", "Mute", &Settings::mute, 0, 1, 1},
        {"audioWeapons", "Weapons (%)", &Settings::weapons, 0, 100, 1},
        {"audioExplosions", "Explosions (%)", &Settings::explosions, 0, 100, 1},
        {"audioVehicles", "Vehicles (%)", &Settings::vehicles, 0, 100, 1},
        {"audioImpacts", "Impacts and debris (%)", &Settings::impacts, 0, 100, 1},
        {"audioBlastMinimum", "Big blast minimum (%)", &Settings::blastMinimum, 0, 100, 1},
        {"audioBlastThreshold", "Big blast radius threshold (m)", &Settings::blastThreshold, 1, 100, 1},
        {"audioFalloff", "Distance falloff strength", &Settings::falloff, 0, 2, 0.05f},
        {"audioDelay", "Speed-of-sound delay", &Settings::delay, 0, 1, 1}
    }};
    inline Settings sanitize(Settings s)
    {
        const Settings defaults;
        for (const auto& f : FIELDS)
            s.*(f.member) = std::isfinite(s.*(f.member)) ? std::clamp(s.*(f.member), f.min, f.max) : defaults.*(f.member);
        s.mute = s.mute >= 0.5f;
        s.delay = s.delay >= 0.5f;
        return s;
    }
    inline float mix(const Settings& settings, Category category, float distance, float reference, float source = 1, float radius = 0)
    {
        const auto s = sanitize(settings);
        const float levels[] = {s.weapons, s.explosions, s.vehicles, s.impacts};
        const float base = std::clamp(reference / std::max(distance, reference), 0.0f, 1.0f);
        const float attenuated = std::pow(base, s.falloff);
        float gain = (attenuated < 0.02f ? 0 : attenuated) * std::max(source, 0.0f);
        if (category == Category::Explosions && radius > s.blastThreshold)
            gain = std::max(gain, s.blastMinimum / 100);
        return s.mute ? 0 : std::clamp(gain * (s.master / 100) * (levels[static_cast<int>(category)] / 100), 0.0f, 1.0f);
    }
    inline float delay(const Settings& s, float distance) { return s.delay >= 0.5f ? std::max(0.0f, distance) / 343.0f : 0; }
    inline std::string toJson(Settings s)
    {
        s = sanitize(s);
        std::string out = "{";
        char buf[128];
        for (const auto& f : FIELDS)
        {
            std::snprintf(buf, sizeof buf, "%s\"%s\":%.9g", out.size() > 1 ? "," : "", f.id, s.*(f.member));
            out += buf;
        }
        return out + "}";
    }
    inline bool fromJson(std::string_view text, Settings& settings)
    {
        Settings s = settings;
        size_t i = 0;
        auto space = [&] { while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t')) ++i; };
        space();
        if (i == text.size() || text[i++] != '{') return false;
        space();
        while (i < text.size() && text[i] != '}')
        {
            if (text[i++] != '"') return false;
            const auto endKey = text.find('"', i);
            if (endKey == text.npos) return false;
            const auto key = text.substr(i, endKey - i);
            i = endKey + 1;
            space();
            if (i == text.size() || text[i++] != ':') return false;
            const std::string number(text.substr(i));
            char* end = nullptr;
            const float value = std::strtof(number.c_str(), &end);
            if (end == number.c_str() || !std::isfinite(value)) return false;
            i += static_cast<size_t>(end - number.c_str());
            for (const auto& f : FIELDS) if (key == f.id) s.*(f.member) = value;
            space();
            if (i < text.size() && text[i] == ',') { ++i; space(); if (i == text.size() || text[i] == '}') return false; }
            else break;
        }
        if (i == text.size() || text[i++] != '}') return false;
        space();
        if (i != text.size()) return false;
        settings = sanitize(s);
        return true;
    }
}
