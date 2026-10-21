// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "weather_location/WindowsLocation.h"

#include <cmath>
#include <limits>
#include <utility>

using namespace aether;
using namespace aether::weather;
using namespace std::chrono_literals;
namespace {
template<class T>void ExpectError(const Result<T>& result,ErrorCode code){REQUIRE_FALSE(result.ok());CHECK_EQ(result.error().code,code);}
}

AETHER_TEST("weather location: valid coordinates become coarse without an invented country"){
    for(const auto [latitude,longitude]:{std::pair{12.345,67.891},std::pair{-12.345,-67.891},std::pair{90.0,180.0},std::pair{-90.0,-180.0}}){
        auto value=CoarsenWindowsLocation(latitude,longitude);REQUIRE(value.ok());
        CHECK_NEAR(value->latitude,std::round(latitude*10)/10,0.000001);
        CHECK_NEAR(value->longitude,std::round(longitude*10)/10,0.000001);
        CHECK_FALSE(value->country.has_value());
    }
    const auto zero=CoarsenWindowsLocation(-0.001,-0.001);REQUIRE(zero.ok());
    CHECK_EQ(zero->latitude,0);CHECK_EQ(zero->longitude,0);
    CHECK_FALSE(std::signbit(zero->latitude));CHECK_FALSE(std::signbit(zero->longitude));
}

AETHER_TEST("weather location: invalid or unknown coordinates never select a default place"){
    const auto nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
    for(const auto [latitude,longitude]:{std::pair{nan,0.0},std::pair{0.0,nan},std::pair{inf,0.0},std::pair{0.0,-inf},
        std::pair{-90.01,0.0},std::pair{90.01,0.0},std::pair{0.0,-180.01},std::pair{0.0,180.01}})
        ExpectError(CoarsenWindowsLocation(latitude,longitude),ErrorCode::InvalidArgument);
}

AETHER_TEST("weather location: only nonfuture samples at most thirty minutes old are fresh"){
    CHECK_FALSE(IsWindowsLocationSampleFresh(-1ms));CHECK(IsWindowsLocationSampleFresh(0ms));
    CHECK(IsWindowsLocationSampleFresh(30min));CHECK_FALSE(IsWindowsLocationSampleFresh(30min+1ms));
}

AETHER_TEST("weather location: absent app consent fails before any runtime or device call"){
    ExpectError(ReadWindowsLocation(false),ErrorCode::AccessDenied);
    ExpectError(BeginWindowsLocationAccess({}),ErrorCode::InvalidArgument);
}

AETHER_TEST("weather location: invalid timeouts and pre-cancelled reads never discover location"){
    for(const auto timeout:{0ms,-1ms,30001ms})ExpectError(ReadWindowsLocation(true,{},timeout),ErrorCode::InvalidArgument);
    CancellationSource source;source.Cancel();
    ExpectError(ReadWindowsLocation(true,source.Token()),ErrorCode::Cancelled);
}
