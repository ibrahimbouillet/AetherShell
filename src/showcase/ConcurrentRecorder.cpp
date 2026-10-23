// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/ConcurrentRecorder.h"
#include <Windows.h>
#include <Ole2.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <optional>
#include <thread>
#include <utility>
namespace aether::showcase {
struct ConcurrentRecorder::Impl {
    Acquire acquire;
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    std::atomic<bool> completed=false;
    std::optional<Error> error;
    std::jthread worker;
    void Run(std::stop_token stop,double duration,std::uint32_t fps) noexcept {
        const HRESULT com=::CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        try {
            if(FAILED(com))error=MakeError(ErrorCode::Platform,"capture worker COM initialization failed",com);
            else while(!stop.stop_requested()) {
                const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                auto result=acquire(elapsed);
                if(!result){error=result.error();break;}
                if(elapsed>=duration)break;
                const double now=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                const auto slot=std::floor(now*fps)+1;
                const auto target=start+std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(slot/fps));
                // Bounded one-millisecond sleeps keep cancellation responsive without busy spinning.
                while(!stop.stop_requested()&&std::chrono::steady_clock::now()<target)::Sleep(1);
            }
        }catch(...){error=MakeError(ErrorCode::Internal,"capture acquisition callback failed");}
        if(SUCCEEDED(com))::CoUninitialize();
        completed.store(true,std::memory_order_release);
    }
};
ConcurrentRecorder::ConcurrentRecorder() noexcept=default;
ConcurrentRecorder::ConcurrentRecorder(std::unique_ptr<Impl> impl) noexcept:impl_(std::move(impl)){}
ConcurrentRecorder::~ConcurrentRecorder(){(void)Finish();}
ConcurrentRecorder::ConcurrentRecorder(ConcurrentRecorder&&) noexcept=default;
ConcurrentRecorder& ConcurrentRecorder::operator=(ConcurrentRecorder&& other) noexcept {
    if(this!=&other){(void)Finish();impl_=std::move(other.impl_);}return *this;
}
Result<ConcurrentRecorder> ConcurrentRecorder::Start(double duration,std::uint32_t fps,Acquire acquire) noexcept {
    if(!std::isfinite(duration)||duration<=0||duration>30||fps<1||fps>120||!acquire)
        return MakeError(ErrorCode::InvalidArgument,"capture worker bounds or callback are invalid");
    try {
        auto impl=std::make_unique<Impl>();impl->acquire=std::move(acquire);
        auto* state=impl.get();
        impl->worker=std::jthread([state,duration,fps](std::stop_token stop){state->Run(stop,duration,fps);});
        return ConcurrentRecorder(std::move(impl));
    }catch(...){return MakeError(ErrorCode::Internal,"capture worker could not start");}
}
Result<bool> ConcurrentRecorder::Poll() const noexcept {
    if(!impl_)return MakeError(ErrorCode::InvalidState,"capture worker is not initialized");
    if(!impl_->completed.load(std::memory_order_acquire))return false;
    if(impl_->error)return *impl_->error;
    return true;
}
double ConcurrentRecorder::ElapsedSeconds() const noexcept {
    return impl_?std::chrono::duration<double>(std::chrono::steady_clock::now()-impl_->start).count():0;
}
Result<void> ConcurrentRecorder::Finish() noexcept {
    if(!impl_)return Ok();
    if(impl_->worker.joinable()){impl_->worker.request_stop();impl_->worker.join();}
    if(impl_->error)return *impl_->error;
    return Ok();
}
}
