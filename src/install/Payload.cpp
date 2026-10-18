// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "install/InstallInternal.h"
#include "core/FileSystem.h"
#include "core/Strings.h"

#include <algorithm>
#include <memory>
#include <unordered_set>

namespace aether::install {
namespace {
struct ModuleCloser { void operator()(HINSTANCE__* module) const noexcept { if(module) ::FreeLibrary(module); } };
using Module=std::unique_ptr<HINSTANCE__,ModuleCloser>;
Result<Module> Open(const std::filesystem::path& path) {
    auto module=::LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) return MakeError(ErrorCode::Io,"cannot open setup payload as data",::GetLastError()); return Module(module);
}
Error Invalid() { return MakeError(ErrorCode::Validation,"invalid setup payload manifest"); }
bool Keys(const json::Value& value,std::initializer_list<std::string_view> keys) {
    if (!value.IsObject()) return false;
    for (const auto& member:*value.AsObject()) if (std::find(keys.begin(),keys.end(),member.key)==keys.end()) return false;
    return true;
}
std::string Lower(std::string value) { for(char& c:value) { if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a'); if(c=='\\') c='/'; } return value; }
}

namespace detail {
Result<std::string> Resource(HMODULE module,std::string_view name,std::uint64_t limit) {
    const auto wide=text::Widen(name); const auto resource=::FindResourceW(module,wide.c_str(),RT_RCDATA);
    if (!resource) return MakeError(ErrorCode::NotFound,"missing setup payload resource",::GetLastError());
    const DWORD size=::SizeofResource(module,resource); if (size>limit) return MakeError(ErrorCode::Integrity,"setup resource exceeds its size bound");
    const auto loaded=::LoadResource(module,resource); const void* bytes=loaded?::LockResource(loaded):nullptr;
    if (!bytes && size!=0) return MakeError(ErrorCode::Io,"cannot read setup resource",::GetLastError());
    return size ? std::string(static_cast<const char*>(bytes),size) : std::string{};
}
}

Result<PayloadManifest> ParsePayloadManifest(std::string_view bytes) {
    try {
        auto parsed=json::Parse(bytes,json::ParseOptions{64*1024,12}); if (!parsed) return parsed.error(); const auto& v=parsed.value();
        if (!Keys(v,{"schemaVersion","product","version","architecture","files"}) || v.GetInt64("schemaVersion",0)!=1 || v.GetString("product")!="AetherShell") return Invalid();
        PayloadManifest manifest; manifest.version=v.GetString("version"); if(!updates::SemanticVersion::Parse(manifest.version)) return Invalid();
        const auto arch=updates::ParseArchitecture(v.GetString("architecture")); if(!arch) return Invalid(); manifest.architecture=*arch;
        const auto* files=v.Find("files"); if(!files || !files->IsArray() || files->AsArray()->empty() || files->AsArray()->size()>64) return Invalid();
        std::uint64_t total=0; std::unordered_set<std::string> names,resources;
        for(const auto& value:*files->AsArray()) {
            if(!Keys(value,{"name","resource","size","sha256"})) return Invalid();
            PayloadFile file; file.name=value.GetString("name"); file.resource=value.GetString("resource");
            if(!detail::SafeName(file.name) || file.resource.empty() || file.resource.size()>128 || Lower(file.name)=="payload-manifest.json") return Invalid();
            for(const char c:file.resource) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) return Invalid();
            if(!names.insert(Lower(file.name)).second || !resources.insert(Lower(file.resource)).second) return Invalid();
            const auto* size=value.Find("size"); const auto count=size?size->AsInt64():std::nullopt;
            if(!count || *count<=0 || static_cast<std::uint64_t>(*count)>updates::kMaxInstallerBytes-total) return Invalid();
            file.size=static_cast<std::uint64_t>(*count); total+=file.size;
            auto hash=updates::ParseSha256Hex(value.GetString("sha256")); if(!hash) return Invalid(); file.sha256=hash.value();
            manifest.files.push_back(std::move(file));
        }
        return manifest;
    } catch(...) { return Invalid(); }
}
Result<PayloadManifest> ReadPayloadManifest(const std::filesystem::path& package) {
    try { auto module=Open(package); if(!module) return module.error(); auto bytes=detail::Resource(module->get(),"PAYLOAD_MANIFEST",64*1024); return bytes?ParsePayloadManifest(bytes.value()):Result<PayloadManifest>(bytes.error()); }
    catch(...) { return MakeError(ErrorCode::Io,"cannot read setup payload"); }
}
Result<void> StagePackage(const std::filesystem::path& package,const InstallLayout& layout,InstallState& state,const CancellationToken& cancellation) {
    try {
        if(cancellation.IsCancelled()) return MakeError(ErrorCode::Cancelled,"installation cancelled before staging");
        auto module=Open(package); if(!module) return module.error();
        auto bytes=detail::Resource(module->get(),"PAYLOAD_MANIFEST",64*1024); if(!bytes) return bytes.error();
        auto manifest=ParsePayloadManifest(bytes.value()); if(!manifest) return manifest.error();
        if(manifest->architecture!=updates::CurrentArchitecture()) return MakeError(ErrorCode::Unsupported,"setup architecture does not match this build");
        auto created=fs::EnsureDirectory(layout.root); if(!created) return created;
        std::uint64_t total=0; for(const auto& file:manifest->files) total+=file.size;
        auto space=fs::AvailableDiskSpace(layout.root); if(!space) return space.error();
        if(space.value()<total*2+1024*1024) return MakeError(ErrorCode::InsufficientSpace,"not enough staging disk space");
        auto id=MakeHealthToken(); if(!id) return id.error();
        state.transaction=InstallTransaction{id.value(),state.activeVersion,manifest->version,"",TransactionState::Prepared,detail::UtcNow(),detail::UtcNow(),""};
        auto saved=SaveState(layout,state); if(!saved) return saved;
        auto verified=detail::WriteState(layout,state,TransactionState::Verified); if(!verified) return verified;
        const auto staging=detail::Staging(layout,*state.transaction); auto safe=detail::Safe(layout,staging); if(!safe) return safe;
        if(std::filesystem::exists(staging)) return MakeError(ErrorCode::AlreadyExists,"staging directory already exists");
        created=fs::EnsureDirectory(staging); if(!created) return created;
        for(const auto& file:manifest->files) {
            if(cancellation.IsCancelled()) { auto ignored=detail::Remove(layout,staging); (void)ignored; return MakeError(ErrorCode::Cancelled,"installation cancelled during staging"); }
            const auto path=staging/text::Widen(file.name); safe=detail::Safe(layout,path); if(!safe) return safe;
            auto data=detail::Resource(module->get(),file.resource,file.size);
            if(!data || data->size()!=file.size) { auto ignored=detail::Remove(layout,staging); (void)ignored; return MakeError(ErrorCode::Integrity,"setup resource size mismatch"); }
            auto hash=updates::Sha256::Hash(data.value());
            if(!hash || hash.value()!=file.sha256) { auto ignored=detail::Remove(layout,staging); (void)ignored; return MakeError(ErrorCode::Integrity,"setup resource digest mismatch"); }
            auto write=fs::WriteFileAtomic(path,data.value()); if(!write) { auto ignored=detail::Remove(layout,staging); (void)ignored; return write; }
        }
        if(cancellation.IsCancelled()) { auto ignored=detail::Remove(layout,staging); (void)ignored; return MakeError(ErrorCode::Cancelled,"installation cancelled during staging"); }
        auto write=fs::WriteFileAtomic(staging/L"payload-manifest.json",bytes.value()); if(!write) return write;
        return detail::WriteState(layout,state,TransactionState::Staged);
    } catch(...) { return MakeError(ErrorCode::Io,"cannot stage setup payload"); }
}
}  // namespace aether::install
