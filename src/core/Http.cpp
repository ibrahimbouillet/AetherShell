// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "core/Http.h"

#include "core/Strings.h"

namespace aether::net {

const std::string* HttpResponse::FindHeader(std::string_view name) const noexcept {
    for (const auto& header : headers) {
        if (text::EqualsIgnoreCaseAscii(header.name, name)) return &header.value;
    }
    return nullptr;
}

}  // namespace aether::net
