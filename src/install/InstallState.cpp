// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/InstallInternal.h"
#include "core/FileSystem.h"
#include "core/Strings.h"

#include <algorithm>
#include <array>
#include <format>
#include <climits>
#include <system_error>

namespace aether::install {
namespace {
constexpr std::array<std::string_view,9> states{"Prepared","Verified","Staged","Applying","Installed",
    "HealthCheckPending","Committed","RollbackRequired","RolledBack"};
Error Invalid() { return MakeError(ErrorCode::Validation, "invalid installation state or path"); }
bool Version(std::string_view value, bool empty = false) { return (empty && value.empty()) || updates::SemanticVersion::Parse(value).ok(); }
bool Keys(const json::Value& v, std::initializer_list<std::string_view> allowed) {
    if (!v.IsObject()) return false;
    for (const auto& member : *v.AsObject()) if (std::find(allowed.begin(), allowed.end(), member.key) == allowed.end()) return false;
    return true;
}
std::optional<std::string> String(const json::Value& v, std::string_view key) {
    const auto* p=v.Find(key); if (!p || !p->IsString()) return {}; return *p->AsString();
}
json::Value JsonState(const InstallState& state) {
    auto v=json::Value::MakeObject(); v.Set("schemaVersion",state.schemaVersion); v.Set("product","AetherShell");
    v.Set("activeVersion",state.activeVersion); v.Set("previousVersion",state.previousVersion);
    if (state.transaction) {
        const auto& t=*state.transaction; auto tx=json::Value::MakeObject();
        tx.Set("id",t.id); tx.Set("fromVersion",t.fromVersion); tx.Set("toVersion",t.toVersion);
        tx.Set("state",states[static_cast<std::size_t>(t.state)]); tx.Set("healthToken",t.healthToken);
        tx.Set("startedUtc",t.startedUtc); tx.Set("updatedUtc",t.updatedUtc); tx.Set("error",t.error); v.Set("transaction",std::move(tx));
    }
    return v;
}
}

Result<InstallLayout> InstallLayout::Create(const std::filesystem::path& root) {
    try {
        if (!root.is_absolute()) return Invalid();
        auto normalized=root.lexically_normal();
        while(normalized.filename().empty() && normalized!=normalized.root_path()) normalized=normalized.parent_path();
        if(normalized==normalized.root_path()) return Invalid();
        InstallLayout layout{std::move(normalized)}; auto safe=detail::Safe(layout,layout.root);
        if (!safe) return safe.error(); return layout;
    } catch (...) { return Invalid(); }
}
Result<std::filesystem::path> InstallLayout::Version(std::string_view version) const {
    if (!updates::SemanticVersion::Parse(version)) return Invalid();
    const auto path=root/L"versions"/text::Widen(version); auto safe=detail::Safe(*this,path);
    if (!safe) return safe.error(); return path;
}
Result<void> ValidateSeparateRoots(const InstallLayout& layout,const std::filesystem::path& dataRoot) {
    auto data=InstallLayout::Create(dataRoot);if(!data)return data.error();
    auto install=InstallLayout::Create(layout.root);if(!install)return install.error();
    auto canonicalData=detail::CanonicalRoot(data->root),canonicalInstall=detail::CanonicalRoot(install->root);
    if(!canonicalData)return canonicalData.error();if(!canonicalInstall)return canonicalInstall.error();
    const auto contains=[](const std::filesystem::path& parent,const std::filesystem::path& child) {
        const auto& a=parent.native();const auto& b=child.native();
        if(b.size()<a.size() || a.size()>INT_MAX)return false;
        if(::CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(a.size()),TRUE)!=CSTR_EQUAL)return false;
        return b.size()==a.size() || b[a.size()]==L'\\' || b[a.size()]==L'/';
    };
    if(contains(canonicalInstall.value(),canonicalData.value()) || contains(canonicalData.value(),canonicalInstall.value()))return MakeError(ErrorCode::Validation,"data and installation roots must be separate");
    return Ok();
}
Result<InstallState> ParseState(std::string_view bytes) {
    try {
        auto parsed=json::Parse(bytes,json::ParseOptions{64*1024,12}); if (!parsed) return parsed.error(); const auto& v=parsed.value();
        if (!Keys(v,{"schemaVersion","product","activeVersion","previousVersion","transaction"}) || v.GetInt64("schemaVersion",0)!=1 || v.GetString("product")!="AetherShell") return Invalid();
        auto active=String(v,"activeVersion"), previous=String(v,"previousVersion");
        if (!active || !previous || !Version(*active,true) || !Version(*previous,true)) return Invalid();
        InstallState state; state.activeVersion=*active; state.previousVersion=*previous;
        if (const auto* tx=v.Find("transaction")) {
            if (!Keys(*tx,{"id","fromVersion","toVersion","state","healthToken","startedUtc","updatedUtc","error"})) return Invalid();
            auto id=String(*tx,"id"), from=String(*tx,"fromVersion"), to=String(*tx,"toVersion"), name=String(*tx,"state");
            if (!id || !from || !to || !name || !String(*tx,"startedUtc") || !String(*tx,"updatedUtc") || !IsHealthToken(*id) || !Version(*from,true) || !Version(*to)) return Invalid();
            const auto found=std::find(states.begin(),states.end(),*name); if (found==states.end()) return Invalid();
            InstallTransaction t; t.id=*id; t.fromVersion=*from; t.toVersion=*to;
            t.state=static_cast<TransactionState>(found-states.begin());
            for (const auto field:{"healthToken","startedUtc","updatedUtc","error"}) {
                if (const auto* value=tx->Find(field); value && !value->IsString()) return Invalid();
            }
            t.healthToken=tx->GetString("healthToken"); if (!t.healthToken.empty() && !IsHealthToken(t.healthToken)) return Invalid();
            t.startedUtc=tx->GetString("startedUtc"); t.updatedUtc=tx->GetString("updatedUtc"); t.error=tx->GetString("error");
            if (t.startedUtc.size()>64 || t.updatedUtc.size()>64 || t.error.size()>512) return Invalid(); state.transaction=std::move(t);
        }
        return state;
    } catch (...) { return Invalid(); }
}
Result<InstallState> LoadState(const InstallLayout& layout) {
    auto safe=detail::Safe(layout,layout.Journal()); if (!safe) return safe.error();
    auto file=fs::ReadFile(layout.Journal(),64*1024); return file ? ParseState(file.value()) : Result<InstallState>(file.error());
}
Result<void> SaveState(const InstallLayout& layout,const InstallState& state) {
    try {
        if (state.transaction && static_cast<std::size_t>(state.transaction->state)>=states.size()) return Invalid();
        const auto bytes=json::Serialize(JsonState(state),json::SerializeOptions{true,2}); auto validated=ParseState(bytes);
        if (!validated) return validated.error(); auto safe=detail::Safe(layout,layout.Journal()); if (!safe) return safe;
        return fs::WriteFileAtomic(layout.Journal(),bytes);
    } catch (...) { return MakeError(ErrorCode::Io,"cannot save installation state"); }
}
Result<void> Transition(InstallState& state,TransactionState next) {
    if (!state.transaction) return Invalid(); const auto current=state.transaction->state;
    const bool forward=(current==TransactionState::Prepared && next==TransactionState::Verified) ||
        (current==TransactionState::Verified && next==TransactionState::Staged) ||
        (current==TransactionState::Staged && next==TransactionState::Applying) ||
        (current==TransactionState::Applying && next==TransactionState::Installed) ||
        (current==TransactionState::Installed && (next==TransactionState::HealthCheckPending || next==TransactionState::Committed)) ||
        (current==TransactionState::HealthCheckPending && next==TransactionState::Committed) ||
        (current==TransactionState::RollbackRequired && next==TransactionState::RolledBack);
    const bool rollback=next==TransactionState::RollbackRequired && current!=TransactionState::Committed && current!=TransactionState::RolledBack && current!=TransactionState::RollbackRequired;
    if (!forward && !rollback) return MakeError(ErrorCode::InvalidState,"illegal installation transition");
    state.transaction->state=next; state.transaction->updatedUtc=detail::UtcNow(); return Ok();
}

namespace detail {
Result<void> Safe(const InstallLayout& layout,const std::filesystem::path& path) {
    try {
        if (!layout.root.is_absolute() || layout.root.filename().empty() || layout.root.lexically_normal()==layout.root.root_path()) return Invalid();
        if (path.lexically_normal()!=layout.root.lexically_normal() && !fs::IsPathWithin(layout.root,path)) return Invalid();
        auto current=layout.root;
        auto check=[](const std::filesystem::path& p) { const DWORD attr=::GetFileAttributesW(p.c_str()); return attr==INVALID_FILE_ATTRIBUTES || (attr&FILE_ATTRIBUTE_REPARSE_POINT)==0; };
        for (auto ancestor=current; !ancestor.empty();) {
            if (!check(ancestor)) return Invalid();
            const auto parent=ancestor.parent_path(); if(parent==ancestor) break; ancestor=parent;
        }
        const auto relative=path.lexically_normal().lexically_relative(layout.root);
        for (const auto& part:relative) { current/=part; if (!check(current)) return Invalid(); }
        return Ok();
    } catch (...) { return Invalid(); }
}
Result<void> Remove(const InstallLayout& layout,const std::filesystem::path& path) {
    auto safe=Safe(layout,path); if (!safe) return safe;
    std::error_code ec;
    if(std::filesystem::is_directory(path,ec)) {
        for(std::filesystem::recursive_directory_iterator it(path,ec),end; !ec && it!=end; it.increment(ec)) {
            safe=Safe(layout,it->path()); if(!safe) return safe;
        }
        if(ec) return MakeError(ErrorCode::Io,"cannot inspect installation files",ec.value());
    }
    ec.clear(); std::filesystem::remove_all(path,ec);
    return ec ? Result<void>(MakeError(ErrorCode::Io,"cannot remove installation files",ec.value())) : Ok();
}
Result<void> CopyAtomic(const std::filesystem::path& from,const std::filesystem::path& to) {
    auto bytes=fs::ReadFile(from,updates::kMaxInstallerBytes); if (!bytes) return bytes.error(); return fs::WriteFileAtomic(to,bytes.value());
}
std::filesystem::path Staging(const InstallLayout& layout,const InstallTransaction& t) { return layout.root/L"staging"/text::Widen(t.toVersion+"."+t.id); }
std::filesystem::path Backup(const InstallLayout& layout,const InstallTransaction& t) { return layout.root/L"staging"/text::Widen(t.toVersion+"."+t.id+".backup"); }
Result<void> WriteState(const InstallLayout& layout,InstallState& state,TransactionState next) { auto changed=Transition(state,next); return changed ? SaveState(layout,state) : changed; }
std::string UtcNow() { SYSTEMTIME time{}; ::GetSystemTime(&time); return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond); }
bool SafeName(std::string_view name) {
    if (name.empty() || name.size()>1024 || name.front()=='/' || name.front()=='\\') return false;
    std::size_t start=0;
    while (start<name.size()) {
        const auto end=name.find_first_of("/\\",start); const auto component=name.substr(start,end==std::string_view::npos?name.size()-start:end-start);
        if (component.empty() || component=="." || component==".." || component.back()=='.' || component.back()==' ') return false;
        std::string base(component.substr(0,component.find('.')));
        for (char& c:base) if (c>='a' && c<='z') c=static_cast<char>(c-'a'+'A');
        if (base=="CON" || base=="PRN" || base=="AUX" || base=="NUL" || (base.size()==4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3]>='1' && base[3]<='9')) return false;
        for (unsigned char c:component) if (c<32 || c>=127 || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|') return false;
        if (end==std::string_view::npos) return true; start=end+1; if (start==name.size()) return false;
    }
    return false;
}
}  // namespace detail
}  // namespace aether::install
