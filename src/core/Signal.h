// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Minimal thread-safe observer list. Connect() returns an RAII Connection;
// destroying it disconnects the slot. Emit() invokes slots outside the lock,
// on the emitting thread.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace aether {

class Connection {
public:
    Connection() = default;
    explicit Connection(std::function<void()> disconnect) : disconnect_(std::move(disconnect)) {}
    Connection(Connection&& other) noexcept : disconnect_(std::exchange(other.disconnect_, nullptr)) {}
    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) {
            Disconnect();
            disconnect_ = std::exchange(other.disconnect_, nullptr);
        }
        return *this;
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection() { Disconnect(); }

    void Disconnect() noexcept {
        if (auto disconnect = std::exchange(disconnect_, nullptr)) {
            try {
                disconnect();
            } catch (...) {
            }
        }
    }
    [[nodiscard]] bool IsConnected() const noexcept { return static_cast<bool>(disconnect_); }

private:
    std::function<void()> disconnect_;
};

template <class... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;

    Signal() : state_(std::make_shared<State>()) {}
    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    [[nodiscard]] Connection Connect(Slot slot) {
        std::lock_guard lock(state_->mutex);
        const auto id = state_->nextId++;
        state_->slots.emplace_back(id, std::make_shared<Slot>(std::move(slot)));
        std::weak_ptr<State> weak = state_;
        return Connection([weak, id] {
            if (auto state = weak.lock()) {
                std::lock_guard lock(state->mutex);
                std::erase_if(state->slots, [id](const auto& entry) { return entry.first == id; });
            }
        });
    }

    void Emit(Args... args) const {
        std::vector<std::shared_ptr<Slot>> snapshot;
        {
            std::lock_guard lock(state_->mutex);
            snapshot.reserve(state_->slots.size());
            for (const auto& entry : state_->slots) snapshot.push_back(entry.second);
        }
        for (const auto& slot : snapshot) (*slot)(args...);
    }

    [[nodiscard]] std::size_t SlotCount() const {
        std::lock_guard lock(state_->mutex);
        return state_->slots.size();
    }

private:
    struct State {
        std::mutex mutex;
        std::vector<std::pair<std::uint64_t, std::shared_ptr<Slot>>> slots;
        std::uint64_t nextId = 1;
    };
    std::shared_ptr<State> state_;
};

}  // namespace aether
