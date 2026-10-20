// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "settings/SettingsService.h"

#include "core/WorkerPool.h"
#include "diagnostics/Logger.h"
#include "settings/SettingsSerialization.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>

namespace aether::settings {
namespace {

template <class Callback, class ResultType>
void Complete(Callback& callback, ResultType result) noexcept {
    if (!callback) return;
    try { callback(std::move(result)); }
    catch (...) { AETHER_LOG_ERROR("settings", "settings completion threw an exception"); }
}

}  // namespace

struct SettingsService::Impl : std::enable_shared_from_this<Impl> {
    Impl(std::shared_ptr<ISettingsStore> persistence, IUiDispatcher& ui)
        : store(std::move(persistence)), dispatcher(ui), worker({1, L"Aether settings", ComApartment::None}) {}

    std::shared_ptr<ISettingsStore> store;
    IUiDispatcher& dispatcher;
    WorkerPool worker;
    std::atomic<bool> alive{true};
    Settings current;
    Signal<const Settings&> changed;
    bool loaded = false;
    bool loading = false;
    bool dirty = false;
    bool newer = false;
    bool changesEnabled = true;  // UI-owned admission gate; accepted worker tasks never consult it
    std::uint64_t revision = 0;
    std::optional<Error> lastSaveError;  // worker-owned; consulted by the FIFO flush barrier

    bool OnUi() const noexcept { return dispatcher.IsUiThread(); }
    void Notify() noexcept {
        try { changed.Emit(current); }
        catch (...) { AETHER_LOG_ERROR("settings", "settings listener threw an exception"); }
    }
    template <class F> void Deliver(F&& action) noexcept {
        try {
            auto state = shared_from_this();
            dispatcher.Post([state, action = std::forward<F>(action)]() mutable {
                if (!state->alive.load()) return;
                try { action(*state); }
                catch (...) { AETHER_LOG_ERROR("settings", "settings delivery threw an exception"); }
            });
        } catch (...) { AETHER_LOG_ERROR("settings", "settings delivery could not be queued"); }
    }
    Result<std::vector<std::string>> Apply(Settings next, bool reset, Completion completion) {
        auto validated = Validate(next);
        if (!validated) return validated.error();
        const auto nextRevision = revision + 1;
        auto self = shared_from_this();
        // Acceptance is atomic with respect to queuing: a rejected task does not change Current.
        if (!worker.Post([self, snapshot = next, reset, nextRevision, completion = std::move(completion)]() mutable {
            Result<void> saved = MakeError(ErrorCode::Internal, "settings store threw an exception");
            try { saved = self->store->Save(snapshot, reset); }
            catch (...) { AETHER_LOG_ERROR("settings", "settings store threw an exception"); }
            self->lastSaveError = saved ? std::optional<Error>{} : std::optional<Error>{saved.error()};
            self->Deliver([saved = std::move(saved), nextRevision, completion = std::move(completion)](Impl& state) mutable {
                if (saved && state.revision == nextRevision) state.dirty = false;
                Complete(completion, std::move(saved));
            });
        })) return MakeError(ErrorCode::InvalidState, "settings worker is stopping");
        current = std::move(next);
        revision = nextRevision;
        dirty = true;
        newer = false;
        Notify();
        return std::move(*validated);
    }
};

SettingsService::SettingsService(std::filesystem::path path, IUiDispatcher& dispatcher) {
    auto store = MakeFileSettingsStore(std::move(path));
    // Invalid paths are reported by Load instead of throwing from the service boundary.
    impl_ = std::make_shared<Impl>(store ? *store : nullptr, dispatcher);
}
SettingsService::SettingsService(std::shared_ptr<ISettingsStore> store, IUiDispatcher& dispatcher)
    : impl_(std::make_shared<Impl>(std::move(store), dispatcher)) {}

SettingsService::~SettingsService() {
    impl_->alive.store(false);
    impl_->worker.Shutdown();
}

Result<void> SettingsService::Load(LoadCompletion completion) {
    try {
        auto state = impl_;
        if (!state->OnUi()) return MakeError(ErrorCode::InvalidState, "settings must be used on the UI thread");
        if (!state->store) return MakeError(ErrorCode::InvalidArgument, "settings store or path is invalid");
        if (state->loading || state->loaded) return MakeError(ErrorCode::InvalidState, "settings already loaded or loading");
        state->loading = true;
        if (!state->worker.Post([state, completion = std::move(completion)]() mutable {
            Result<LoadedSettings> result = MakeError(ErrorCode::Internal, "settings store threw an exception");
            try { result = state->store->Load(); }
            catch (...) { AETHER_LOG_ERROR("settings", "settings store threw an exception"); }
            state->Deliver([result = std::move(result), completion = std::move(completion)](Impl& ui) mutable {
                ui.loading = false;
                ui.loaded = result.ok();
                if (!result) { Complete(completion, Result<LoadReport>(result.error())); return; }
                ui.current = std::move(result->value);
                ui.newer = result->report.status == LoadStatus::NewerSchema;
                ui.Notify();
                Complete(completion, Result<LoadReport>(std::move(result->report)));
            });
        })) {
            state->loading = false;
            return MakeError(ErrorCode::InvalidState, "settings worker is stopping");
        }
        return Ok();
    } catch (...) { return MakeError(ErrorCode::Internal, "settings load could not be queued"); }
}

const Settings& SettingsService::Current() const { assert(impl_->OnUi()); return impl_->current; }
bool SettingsService::Loaded() const { assert(impl_->OnUi()); return impl_->loaded; }
bool SettingsService::Dirty() const { assert(impl_->OnUi()); return impl_->dirty; }
bool SettingsService::NewerSchemaProtected() const { assert(impl_->OnUi()); return impl_->newer; }
Signal<const Settings&>& SettingsService::Changed() { assert(impl_->OnUi()); return impl_->changed; }

Result<void> SettingsService::SetChangesEnabled(bool enabled) {
    try {
        if (!impl_->OnUi()) return MakeError(ErrorCode::InvalidState, "settings must be used on the UI thread");
        impl_->changesEnabled = enabled;
        return Ok();
    } catch (...) { return MakeError(ErrorCode::Internal, "settings change admission could not be updated"); }
}

Result<std::vector<std::string>> SettingsService::Update(Mutator mutator, Completion completion) {
    try {
        if (!impl_->OnUi()) return MakeError(ErrorCode::InvalidState, "settings must be used on the UI thread");
        if (!impl_->loaded || impl_->loading) return MakeError(ErrorCode::InvalidState, "settings have not finished loading");
        if (!impl_->changesEnabled) return MakeError(ErrorCode::InvalidState, "settings changes are disabled for shutdown");
        if (!mutator) return MakeError(ErrorCode::InvalidArgument, "settings mutator is empty");
        Settings next = impl_->current;
        mutator(next);
        return impl_->Apply(std::move(next), false, std::move(completion));
    } catch (...) { return MakeError(ErrorCode::Internal, "settings update failed"); }
}

Result<std::vector<std::string>> SettingsService::ResetToDefaults(Completion completion) {
    try {
        if (!impl_->OnUi()) return MakeError(ErrorCode::InvalidState, "settings must be used on the UI thread");
        if (!impl_->loaded || impl_->loading) return MakeError(ErrorCode::InvalidState, "settings have not finished loading");
        if (!impl_->changesEnabled) return MakeError(ErrorCode::InvalidState, "settings changes are disabled for shutdown");
        // D23: resetting customization never hides or restores the Windows taskbar by itself.
        Settings next{};
        next.general.replaceWindowsTaskbar = impl_->current.general.replaceWindowsTaskbar;
        return impl_->Apply(std::move(next), true, std::move(completion));
    } catch (...) { return MakeError(ErrorCode::Internal, "settings reset failed"); }
}

Result<void> SettingsService::FlushAsync(Completion completion) {
    try {
        auto state = impl_;
        if (!state->OnUi()) return MakeError(ErrorCode::InvalidState, "settings must be used on the UI thread");
        if (!state->loaded || state->loading) return MakeError(ErrorCode::InvalidState, "settings have not finished loading");
        if (!state->worker.Post([state, completion = std::move(completion)]() mutable {
            Result<void> result = state->lastSaveError ? Result<void>(*state->lastSaveError) : Ok();
            state->Deliver([result = std::move(result), completion = std::move(completion)](Impl&) mutable {
                Complete(completion, std::move(result));
            });
        })) return MakeError(ErrorCode::InvalidState, "settings worker is stopping");
        return Ok();
    } catch (...) { return MakeError(ErrorCode::Internal, "settings flush could not be queued"); }
}

}  // namespace aether::settings
