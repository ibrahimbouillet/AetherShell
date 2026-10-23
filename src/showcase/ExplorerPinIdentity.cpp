// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/ExplorerPinIdentity.h"
#include "core/Json.h"
#include "core/Strings.h"
#include <algorithm>
namespace aether::showcase {
Result<std::wstring> ResolveExplorerPinName(std::string_view settingsJson,std::wstring_view trustedPath) noexcept {
    try {
        if(settingsJson.size()>1024U*1024U||trustedPath.empty())return MakeError(ErrorCode::Validation,"scene settings exceed supported bounds");
        auto parsed=json::Parse(settingsJson,{1024U*1024U,32});
        if(!parsed)return MakeError(ErrorCode::Validation,"scene settings are invalid");
        const auto* dock=parsed->Find("dock");
        const auto* pins=dock?dock->Find("pinned"):nullptr;
        if(!pins||!pins->AsArray()||pins->AsArray()->size()>256)return MakeError(ErrorCode::Validation,"scene pins are unavailable");
        std::wstring result;
        for(const auto& pin:*pins->AsArray()){
            const auto executable=text::Widen(pin.GetString("executablePath"));
            const auto aumid=text::Widen(pin.GetString("appUserModelId"));
            const auto parsing=text::Widen(pin.GetString("parsingName"));
            const bool explorer=text::EqualsIgnoreCase(executable,trustedPath)||
                text::EqualsIgnoreCase(aumid,L"Microsoft.Windows.Explorer")||
                text::EqualsIgnoreCase(parsing,L"Microsoft.Windows.Explorer");
            if(!explorer)continue;
            const auto name=text::Widen(pin.GetString("displayName"));
            const auto id=pin.GetString("id");
            if(!result.empty()||id.empty()||id.size()>512||name.empty()||name.size()>256||
                std::any_of(name.begin(),name.end(),[](wchar_t ch){return ch<32||ch==L'\\'||ch==L'/'||ch==L':'||ch==L'@';}))
                return MakeError(ErrorCode::Validation,"the Explorer pin identity or unique display name is unavailable");
            result=name;
        }
        if(result.empty())return MakeError(ErrorCode::NotFound,"the scene has no trusted Windows Explorer pin");
        return result;
    }catch(...){return MakeError(ErrorCode::Internal,"Explorer pin verification failed");}
}
}
