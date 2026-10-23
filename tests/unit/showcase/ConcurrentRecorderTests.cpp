// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/ConcurrentRecorder.h"
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
using namespace aether;
using namespace aether::showcase;
AETHER_TEST("showcase recorder: acquisition advances while the action coordinator is blocked") {
    std::atomic<int> acquired=0;
    const auto controller=std::this_thread::get_id();
    std::atomic<bool> separate=true;
    auto worker=ConcurrentRecorder::Start(1,100,[&](double)->Result<void>{
        if(std::this_thread::get_id()==controller)separate=false;
        ++acquired;return Ok();
    });
    REQUIRE(worker);
    ::Sleep(150); // Simulated slow UIA provider; no native app or capture is involved.
    CHECK(acquired.load()>=3);CHECK(separate.load());
    auto poll=worker->Poll();REQUIRE(poll);CHECK(!*poll);
    REQUIRE(worker->Finish());
    const int stopped=acquired.load();::Sleep(30);CHECK_EQ(acquired.load(),stopped);
}
AETHER_TEST("showcase recorder: callback failure and exceptions are latched until joined") {
    for(bool throws:{false,true}){
        auto worker=ConcurrentRecorder::Start(1,60,[&](double)->Result<void>{
            if(throws)throw std::runtime_error("synthetic callback");
            return MakeError(ErrorCode::Unavailable,"synthetic acquisition failure");
        });
        REQUIRE(worker);
        for(int retry=0;retry<100;++retry){if(!worker->Poll())break;::Sleep(2);}
        CHECK(!worker->Poll());CHECK(!worker->Finish());CHECK(!worker->Finish());
    }
}
AETHER_TEST("showcase recorder: normal completion covers the duration and invalid inputs start nothing") {
    double finalTimestamp=0;
    auto worker=ConcurrentRecorder::Start(0.05,120,[&](double time)->Result<void>{finalTimestamp=time;return Ok();});
    REQUIRE(worker);
    bool complete=false;
    for(int retry=0;retry<150;++retry){auto poll=worker->Poll();REQUIRE(poll);if(*poll){complete=true;break;}::Sleep(2);}
    REQUIRE(complete);REQUIRE(worker->Finish());CHECK(finalTimestamp>=0.05);
    CHECK(!ConcurrentRecorder::Start(31,60,[](double){return Ok();}));
    CHECK(!ConcurrentRecorder::Start(1,121,[](double){return Ok();}));
    CHECK(!ConcurrentRecorder::Start(1,60,{}));
}
