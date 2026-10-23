// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/PresentationPlan.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>

namespace aether::showcase {
bool IsPresentationShotAction(PresentationActionKind kind) noexcept {
    return kind!=PresentationActionKind::VolumeOffset&&kind!=PresentationActionKind::RestoreVolume&&
        kind!=PresentationActionKind::ClosePanels;
}
Result<void> ValidatePresentationObservations(const PresentationPlan& plan,std::span<const double> started,
    std::span<const double> completed,std::size_t appliedCount) noexcept {
    if(appliedCount!=plan.actions.size()||started.size()!=plan.actions.size()||completed.size()!=plan.actions.size())
        return MakeError(ErrorCode::Timeout,"capture ended before every presentation action completed");
    for(std::size_t index=0;index<plan.actions.size();++index){
        if(!std::isfinite(started[index])||!std::isfinite(completed[index])||started[index]<0||
            completed[index]<started[index]||completed[index]>=plan.durationSeconds||
            (index>0&&started[index]<completed[index-1]))
            return MakeError(ErrorCode::Validation,"presentation action observations are incomplete or inconsistent");
        if(!IsPresentationShotAction(plan.actions[index].kind))continue;
        double nextChange=plan.durationSeconds;
        for(std::size_t next=index+1;next<plan.actions.size();++next){
            const auto kind=plan.actions[next].kind;
            if(kind!=PresentationActionKind::VolumeOffset&&kind!=PresentationActionKind::RestoreVolume){nextChange=started[next];break;}
        }
        if(completed[index]+0.8>=nextChange||completed[index]+0.8>=plan.durationSeconds)
            return MakeError(ErrorCode::Timeout,"a presentation action left no verified settled still interval");
    }
    return Ok();
}
Result<void> ValidatePresentationPlan(const PresentationPlan& plan) noexcept {
    if (!std::isfinite(plan.durationSeconds) || plan.durationSeconds < 1 || plan.durationSeconds > 30 ||
        !std::isfinite(plan.stillAtSeconds) || plan.stillAtSeconds < 0 ||
        plan.stillAtSeconds >= plan.durationSeconds || plan.actions.size() > 128)
        return MakeError(ErrorCode::Validation, "presentation duration, still time or action count is invalid");
    double previous = -1;
    for (const auto& action : plan.actions) {
        const auto kind = static_cast<int>(action.kind);
        if (!std::isfinite(action.atSeconds) || action.atSeconds < 0 || action.atSeconds >= plan.durationSeconds ||
            action.atSeconds < previous || !std::isfinite(action.value) || kind < 1 || kind > 10 ||
            (action.kind == PresentationActionKind::VolumeOffset ? std::abs(action.value) > 30 :
             action.kind == PresentationActionKind::SettingsPage ?
                (action.value < 0 || action.value > 7 || std::floor(action.value) != action.value) : action.value != 0))
            return MakeError(ErrorCode::Validation, "presentation action is invalid or out of order");
        previous = action.atSeconds;
    }
    return Ok();
}

Result<PresentationPlan> MakePresentationPlan(std::string_view name) noexcept {
    try {
        PresentationPlan plan;
        if (name == "desktop") return plan;
        const bool fullGallery = name == "gallery-full" || name == "gallery-full-volume";
        const bool gallery = name == "gallery" || name == "gallery-volume" || fullGallery;
        const bool volume = name == "volume" || name == "gallery-volume" || name == "gallery-full-volume";
        if (name == "quick-controls" || volume || gallery)
            plan.actions.push_back({0.5, PresentationActionKind::OpenQuickControls, 0});
        else if (name == "settings")
            plan.actions.push_back({0.5, PresentationActionKind::OpenSettings, 0});
        else return MakeError(ErrorCode::InvalidArgument, "unknown presentation scenario");
        if (volume) {
            plan.durationSeconds = 7;
            plan.stillAtSeconds = 3;
            // Bounded 30-Hz targets drive the retained endpoint with real UIA slider readback.
            for (int step = 0; step <= 60; ++step) {
                const double t = static_cast<double>(step) / 60;
                const double excursion = 0.5 - 0.5 * std::cos(t * 6.283185307179586);
                plan.actions.push_back({1.5 + 2 * t, PresentationActionKind::VolumeOffset, -20 * excursion});
            }
            plan.actions.push_back({4, PresentationActionKind::RestoreVolume, 0});
        }
        if (gallery) {
            plan.durationSeconds = fullGallery ? 24.5 : 17;
            plan.stillAtSeconds = 2;
            if(fullGallery){
                plan.actions.push_back({4.6, PresentationActionKind::QuickControlsNextPage, 0});
                plan.actions.push_back({6, PresentationActionKind::ClosePanels, 0});
                plan.actions.push_back({6.4, PresentationActionKind::OpenSearchExplorer, 0});
                plan.actions.push_back({8.2, PresentationActionKind::ClosePanels, 0});
                plan.actions.push_back({8.6, PresentationActionKind::DockExplorerMenu, 0});
                plan.actions.push_back({10, PresentationActionKind::ClosePanels, 0});
                plan.actions.push_back({10.4, PresentationActionKind::TopBarMenu, 0});
                plan.actions.push_back({11.8, PresentationActionKind::ClosePanels, 0});
            }else plan.actions.push_back({4.2, PresentationActionKind::ClosePanels, 0});
            plan.actions.push_back({fullGallery ? 12.2 : 4.6, PresentationActionKind::OpenSettings, 0});
            for(int page=0;page<8;++page)
                plan.actions.push_back({(fullGallery ? 13.5 : 5.8) + page * 1.3, PresentationActionKind::SettingsPage, static_cast<double>(page)});
        }
        return plan;
    } catch (...) { return MakeError(ErrorCode::Internal, "presentation plan allocation failed"); }
}

Result<PresentationPlan> ParsePresentationPlan(std::string_view text) noexcept {
    if (text.size() > 16'384) return MakeError(ErrorCode::Validation, "presentation plan is too large");
    try {
        std::istringstream input{std::string(text)};
        std::string header;
        PresentationPlan plan;
        std::size_t count = 0;
        if (!(input >> header) || header != "AETHER_PRESENTATION_1" ||
            !(input >> plan.durationSeconds >> plan.stillAtSeconds >> count) || count > 128)
            return MakeError(ErrorCode::Parse, "presentation plan header is invalid");
        plan.actions.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            PresentationAction action;
            int kind = 0;
            if (!(input >> action.atSeconds >> kind >> action.value))
                return MakeError(ErrorCode::Parse, "presentation plan action is incomplete");
            action.kind = static_cast<PresentationActionKind>(kind);
            plan.actions.push_back(action);
        }
        input >> std::ws;
        if (!input.eof()) return MakeError(ErrorCode::Parse, "presentation plan has trailing data");
        if (auto valid = ValidatePresentationPlan(plan); !valid) return valid.error();
        return plan;
    } catch (...) { return MakeError(ErrorCode::Internal, "presentation plan parsing failed"); }
}

PresentationSession::~PresentationSession() { (void)Restore(); }
Result<void> PresentationSession::Begin(PresentationPlan plan, bool allowDeviceChanges) noexcept {
    if (active_) return MakeError(ErrorCode::InvalidState, "presentation is already active");
    if (auto valid = ValidatePresentationPlan(plan); !valid) return valid.error();
    const bool volume = std::any_of(plan.actions.begin(), plan.actions.end(), [](const auto& action) {
        return action.kind == PresentationActionKind::VolumeOffset;
    });
    if (volume && !allowDeviceChanges)
        return MakeError(ErrorCode::AccessDenied, "the volume scenario requires explicit device-change authorization");
    try {
        plan_ = std::move(plan);
        active_ = true; // Roll back even when Prepare partially fails.
        next_ = 0;
        previous_ = -1;
        if (auto prepared = actions_.Prepare(volume); !prepared) { (void)Restore(); return prepared.error(); }
        return Ok();
    } catch (...) { (void)Restore(); return MakeError(ErrorCode::Internal, "presentation preparation failed"); }
}
Result<void> PresentationSession::Tick(double elapsedSeconds, const ActionObserver& observer) noexcept {
    if (!active_) return MakeError(ErrorCode::InvalidState, "presentation is not active");
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0 || elapsedSeconds < previous_)
        return MakeError(ErrorCode::InvalidArgument, "presentation time must increase monotonically");
    previous_ = elapsedSeconds;
    try {
        while (next_ < plan_.actions.size() && plan_.actions[next_].atSeconds <= elapsedSeconds) {
            if(observer)observer(next_,true);
            if (auto applied = actions_.Apply(plan_.actions[next_]); !applied) {
                if(restoreImmediatelyOnFailure_)(void)Restore();
                return applied.error();
            }
            if(observer)observer(next_,false);
            ++next_;
        }
        return Ok();
    } catch (...) {
        if(restoreImmediatelyOnFailure_)(void)Restore();
        return MakeError(ErrorCode::Internal, "presentation action failed");
    }
}
Result<void> PresentationSession::Restore() noexcept {
    if (!active_) return Ok();
    active_ = false;
    return actions_.Restore();
}
}  // namespace aether::showcase
