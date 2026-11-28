#pragma once
// Estado compartido entre el obs.dll de prueba y link_obs_sim.
#include "../obs/ObsApi.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct FakeData
{
    long long channel = -1;
    long long buffer = -1;
};

struct FakeProperty
{
    std::string name, description;
    bool wellFormed = false;
    std::vector<std::pair<std::string, long long>> items;
    std::string longDescription;
};

struct FakeObsState
{
    std::mutex mutex;
    obs_source_info info{};
    std::size_t registeredSize = 0;
    int registrations = 0;
    obs_source_t *expectedSource = nullptr;
    std::uint64_t frames = 0, calls = 0, badCalls = 0, timestampRegressions = 0, nextTimestamp = 0;
    float peak = 0;
    std::vector<FakeProperty> properties;
    std::vector<std::string> log;
};
