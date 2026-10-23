// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "showcase/NativePresentationActions.h"
#include <Ole2.h>
#include <UIAutomation.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <ShlObj.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace aether::showcase {
namespace {
using Microsoft::WRL::ComPtr;
struct WindowSearch { DWORD process; std::wstring_view type; HWND result = nullptr; };
HWND OwnedWindow(DWORD process, std::wstring_view type) noexcept {
    WindowSearch search{process,type};
    (void)::EnumWindows([](HWND window, LPARAM value)->BOOL {
        auto& s = *reinterpret_cast<WindowSearch*>(value);
        DWORD pid = 0;
        (void)::GetWindowThreadProcessId(window, &pid);
        if (pid != s.process || !::IsWindowVisible(window)) return TRUE;
        wchar_t name[128]{};
        (void)::GetClassNameW(window,name,128);
        if (std::wstring_view(name) == s.type) { s.result = window; return FALSE; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
Error ActionError(HRESULT status) { return MakeError(ErrorCode::Unavailable,"a real Aether presentation control is unavailable",status); }
}
struct NativePresentationActions::Impl {
    HWND dock = nullptr, topbar = nullptr;
    HWND menuOwner = nullptr;
    DWORD process = 0;
    ComPtr<IUIAutomation> automation;
    ComPtr<IAudioEndpointVolume> endpoint;
    ComPtr<IMMDeviceEnumerator> devices;
    std::wstring endpointId;
    std::wstring explorerPinName;
    float originalScalar = 0;
    double originalRange = 0;
    bool volumePrepared = false, volumeChanged = false, ownsQuick = false, ownsSettings = false, ownsMenu = false, ownsSearch = false;
    bool volumePending = false;
    std::optional<int> acknowledgedRange;
    std::optional<int> lastTarget;
    std::optional<float> lastEndpointScalar;
    std::optional<float> lastRestoredScalar;
    std::optional<double> lastDisplayedRange;
    HRESULT lastEndpointStatus = E_PENDING, lastDisplayStatus = E_PENDING;
    HRESULT lastRestoreStatus = E_PENDING;
    ULONGLONG volumePreparedTick = 0;
    bool volumeTouched = false, restorationVerified = false;
    int restoredStableSamples = 0;
    void WriteVolumeReceipt(std::string_view phase) const noexcept {
        // Failure/restoration evidence is private and numeric only. Never include device names,
        // endpoint identifiers, window titles or paths, and never write a per-frame log.
        try {
            wchar_t* raw = nullptr;
            const HRESULT status = ::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw);
            std::unique_ptr<wchar_t, decltype(&::CoTaskMemFree)> folder(raw, &::CoTaskMemFree);
            if (FAILED(status) || !raw) return;
            std::filesystem::path directory(raw);
            directory /= L"AetherShell"; directory /= L"Showcase";
            std::filesystem::create_directories(directory);
            std::ofstream receipt(directory / L"last-volume-restoration.private.txt", std::ios::binary | std::ios::trunc);
            receipt << std::setprecision(9)
                << "privacy=local-only-numeric-device-evidence-do-not-publish\nphase=" << phase
                << "\nprepared_tick_ms=" << volumePreparedTick << "\nsampled_tick_ms=" << ::GetTickCount64()
                << "\nprocess_id=" << ::GetCurrentProcessId()
                << "\noriginal_scalar=" << originalScalar << "\noriginal_display_range=" << originalRange
                << "\nvolume_write_attempted=" << volumeTouched << "\nrestoration_verified=" << restorationVerified
                << "\nrestored_stable_samples=" << restoredStableSamples
                << "\nendpoint_read_hresult=" << lastEndpointStatus << "\ndisplay_read_hresult=" << lastDisplayStatus
                << "\nrestore_hresult=" << lastRestoreStatus;
            receipt << "\nlast_target="; if (lastTarget) receipt << *lastTarget; else receipt << "unknown";
            receipt << "\nlast_endpoint_scalar="; if (lastEndpointScalar) receipt << *lastEndpointScalar; else receipt << "unknown";
            receipt << "\nlast_display_range="; if (lastDisplayedRange) receipt << *lastDisplayedRange; else receipt << "unknown";
            receipt << "\nlast_restored_scalar="; if (lastRestoredScalar) receipt << *lastRestoredScalar; else receipt << "unknown";
            receipt << '\n';
        } catch (...) { /* A failed diagnostic must not prevent device cleanup. */ }
    }
    Result<ComPtr<IUIAutomationElement>> Find(HWND window, PROPERTYID property, const wchar_t* text, bool unique = false) {
        DWORD pid = 0;
        (void)::GetWindowThreadProcessId(window,&pid);
        if (!window || pid != process || !::IsWindowVisible(window)) return ActionError(E_ACCESSDENIED);
        ComPtr<IUIAutomationElement> root, element;
        HRESULT status = automation->ElementFromHandle(window,&root);
        VARIANT value{};
        value.vt = VT_BSTR; value.bstrVal = ::SysAllocString(text);
        if (!value.bstrVal) return ActionError(E_OUTOFMEMORY);
        ComPtr<IUIAutomationCondition> condition;
        if (SUCCEEDED(status)) status = automation->CreatePropertyCondition(property,value,&condition);
        ::VariantClear(&value);
        if (SUCCEEDED(status) && unique) {
            ComPtr<IUIAutomationElementArray> matches;
            status=root->FindAll(TreeScope_Descendants,condition.Get(),&matches);
            int count=0;
            if(SUCCEEDED(status))status=matches->get_Length(&count);
            if(SUCCEEDED(status)&&count!=1)return MakeError(ErrorCode::Validation,"a scene control is absent or ambiguous");
            if(SUCCEEDED(status))status=matches->GetElement(0,&element);
        } else if (SUCCEEDED(status)) status = root->FindFirst(TreeScope_Descendants,condition.Get(),&element);
        if (FAILED(status) || !element) return ActionError(status);
        return element;
    }
    Result<void> Invoke(HWND window, PROPERTYID property, const wchar_t* text) {
        auto element = Find(window,property,text);
        if (!element) return element.error();
        ComPtr<IUIAutomationInvokePattern> pattern;
        HRESULT status = (*element)->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&pattern));
        if (SUCCEEDED(status)) status = pattern->Invoke();
        return SUCCEEDED(status) ? Ok() : Result<void>(ActionError(status));
    }
    Result<void> OpenQuick() {
        if (OwnedWindow(process,L"AetherShell.ControlCenter")) return Ok();
        auto invoked = Invoke(topbar,UIA_AutomationIdPropertyId,L"item-10");
        if (!invoked) return invoked;
        ownsQuick = true;
        for (int retry=0;retry<40;++retry) { if(OwnedWindow(process,L"AetherShell.ControlCenter"))return Ok(); ::Sleep(25); }
        return MakeError(ErrorCode::Timeout,"Aether quick controls did not appear");
    }
    Result<void> WaitForMenu() {
        for(int retry=0;retry<40;++retry){if(OwnedWindow(process,L"#32768"))return Ok();::Sleep(25);}
        return MakeError(ErrorCode::Timeout,"the real Aether menu did not appear");
    }
    Result<void> ExplorerMenu() {
        if(explorerPinName.empty())return MakeError(ErrorCode::Validation,"the Explorer scene identity was not supplied");
        auto item=Find(dock,UIA_NamePropertyId,explorerPinName.c_str(),true);
        if(!item)return item.error();
        BSTR state=nullptr;
        HRESULT status=(*item)->get_CurrentItemStatus(&state);
        const bool pinned=SUCCEEDED(status)&&state&&std::wstring_view(state).starts_with(L"Pinned");
        if(state)::SysFreeString(state);
        RECT bounds{};BOOL enabled=FALSE,offscreen=TRUE;
        if(!pinned||FAILED((*item)->get_CurrentIsEnabled(&enabled))||!enabled||
            FAILED((*item)->get_CurrentIsOffscreen(&offscreen))||offscreen||
            FAILED((*item)->get_CurrentBoundingRectangle(&bounds))||bounds.right<=bounds.left||bounds.bottom<=bounds.top)
            return MakeError(ErrorCode::Unavailable,"the verified Explorer pin is not uniquely visible and pinned");
        const int left=::GetSystemMetrics(SM_XVIRTUALSCREEN),top=::GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int width=::GetSystemMetrics(SM_CXVIRTUALSCREEN),height=::GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if(width<=1||height<=1)return ActionError(E_FAIL);
        const double x=(static_cast<double>(bounds.left)+bounds.right)/2,y=(static_cast<double>(bounds.top)+bounds.bottom)/2;
        INPUT input[3]{};
        input[0].type=INPUT_MOUSE;
        input[0].mi.dx=static_cast<LONG>(std::lround((x-left)*65535/(width-1)));
        input[0].mi.dy=static_cast<LONG>(std::lround((y-top)*65535/(height-1)));
        input[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
        input[1].type=INPUT_MOUSE;input[1].mi.dwFlags=MOUSEEVENTF_RIGHTDOWN;
        input[2].type=INPUT_MOUSE;input[2].mi.dwFlags=MOUSEEVENTF_RIGHTUP;
        ownsMenu=true;menuOwner=dock;
        if(::SendInput(3,input,sizeof(INPUT))!=3)return ActionError(E_FAIL);
        return WaitForMenu();
    }
    Result<void> SearchExplorer() {
        ownsSearch=true;
        auto opened=Invoke(dock,UIA_NamePropertyId,L"Search applications");
        if(!opened)return opened;
        HWND window=nullptr;
        for(int retry=0;retry<40&&!window;++retry){window=OwnedWindow(process,L"AetherShell.Search");if(!window)::Sleep(25);}
        const HWND edit=window?::GetDlgItem(window,100):nullptr;
        DWORD pid=0;(void)::GetWindowThreadProcessId(edit,&pid);
        if(!edit||pid!=process)return ActionError(E_ACCESSDENIED);
        ComPtr<IUIAutomationElement> element;ComPtr<IUIAutomationValuePattern> value;
        HRESULT status=automation->ElementFromHandle(edit,&element);
        if(SUCCEEDED(status))status=element->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&value));
        BSTR query=::SysAllocString(L"explor");
        if(!query)return ActionError(E_OUTOFMEMORY);
        if(SUCCEEDED(status))status=value->SetValue(query);
        ::SysFreeString(query);
        return SUCCEEDED(status)?Ok():Result<void>(ActionError(status));
    }
    Result<void> ValidateSearch() {
        const HWND search=OwnedWindow(process,L"AetherShell.Search");
        if(!search)return Ok();
        ComPtr<IUIAutomationElement> root;
        HRESULT status=automation->ElementFromHandle(search,&root);
        VARIANT type{};type.vt=VT_I4;type.lVal=UIA_ListItemControlTypeId;
        ComPtr<IUIAutomationCondition> condition;ComPtr<IUIAutomationElementArray> results;
        if(SUCCEEDED(status))status=automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&condition);
        if(SUCCEEDED(status))status=root->FindAll(TreeScope_Children,condition.Get(),&results);
        int count=0;if(SUCCEEDED(status))status=results->get_Length(&count);
        if(FAILED(status)||count>64)return ActionError(status);
        for(int index=0;index<count;++index){
            ComPtr<IUIAutomationElement> result;BSTR name=nullptr;
            status=results->GetElement(index,&result);
            if(SUCCEEDED(status))status=result->get_CurrentName(&name);
            const std::wstring_view label=name?std::wstring_view(name):std::wstring_view{};
            const bool allowed=label==L"File Explorer"||label==L"Windows Explorer"||label==L"Explorer"||
                label==L"Explorateur de fichiers"||(!explorerPinName.empty()&&label==explorerPinName);
            if(name)::SysFreeString(name);
            if(FAILED(status)||!allowed)return MakeError(ErrorCode::AccessDenied,"search returned an unreviewed application name; no presentation was finalized");
        }
        return Ok();
    }
    Result<ComPtr<IUIAutomationRangeValuePattern>> Volume() {
        const HWND center = OwnedWindow(process,L"AetherShell.ControlCenter");
        auto element = Find(center,UIA_AutomationIdPropertyId,L"item-301");
        if (!element) return element.error();
        ComPtr<IUIAutomationRangeValuePattern> range;
        HRESULT status=(*element)->GetCurrentPatternAs(UIA_RangeValuePatternId,IID_PPV_ARGS(&range));
        if (FAILED(status)) return ActionError(status);
        return range;
    }
    bool CurrentEndpointMatches() noexcept {
        ComPtr<IMMDevice> currentDevice;
        HRESULT current=devices->GetDefaultAudioEndpoint(eRender,eConsole,&currentDevice);
        wchar_t* identifier=nullptr;
        if(SUCCEEDED(current))current=currentDevice->GetId(&identifier);
        const bool same=SUCCEEDED(current)&&identifier&&endpointId==identifier;
        if(identifier)::CoTaskMemFree(identifier);
        return same;
    }
    Result<void> SetVolume(double value) {
        if(!CurrentEndpointMatches())return MakeError(ErrorCode::Unavailable,"the audio endpoint changed during the presentation");
        const int target=static_cast<int>(std::lround(std::clamp(value,0.0,100.0)));
        if(!volumePending && acknowledgedRange==target)return Ok();
        auto range=Volume();if(!range)return range.error();
        volumeChanged=true;volumePending=true;volumeTouched=true;restorationVerified=false;
        lastTarget=target;lastEndpointScalar.reset();lastDisplayedRange.reset();
        lastEndpointStatus=lastDisplayStatus=E_PENDING;
        // Bind the authorized write to the retained endpoint, never to an asynchronously retargeted
        // application request. The real Aether slider is observed through UIA, not fabricated.
        const HRESULT status=endpoint->SetMasterVolumeLevelScalar(static_cast<float>(target)/100,nullptr);
        if(FAILED(status)){WriteVolumeReceipt("write-failed");return ActionError(status);}
        // Every distinct target is observed on both the endpoint and real UI before the next write.
        for(int retry=0;retry<60;++retry){
            if(!CurrentEndpointMatches()){
                WriteVolumeReceipt("default-endpoint-changed");
                return MakeError(ErrorCode::Unavailable,"the audio endpoint changed during the presentation");
            }
            float actual=0;double displayed=0;
            lastEndpointScalar.reset();lastDisplayedRange.reset();
            lastEndpointStatus=endpoint->GetMasterVolumeLevelScalar(&actual);
            lastDisplayStatus=(*range)->get_CurrentValue(&displayed);
            if(SUCCEEDED(lastEndpointStatus)&&std::isfinite(actual))lastEndpointScalar=actual;
            if(SUCCEEDED(lastDisplayStatus)&&std::isfinite(displayed))lastDisplayedRange=displayed;
            if(SUCCEEDED(lastEndpointStatus) &&
                std::abs(actual-static_cast<float>(target)/100)<0.000001f &&
                SUCCEEDED(lastDisplayStatus)&&std::abs(displayed-target)<0.001){
                acknowledgedRange=target;volumePending=false;return Ok();
            }
            ::Sleep(10);
        }
        WriteVolumeReceipt("acknowledgement-failed");
        return MakeError(ErrorCode::Timeout,"the real volume change was not acknowledged");
    }
    Result<void> RestoreVolume() noexcept {
        if (!volumeChanged) return Ok();
        try {
            restoredStableSamples=0;lastRestoredScalar.reset();
            // All writes were bound directly to this retained endpoint. No asynchronous Aether
            // volume request remains that can later redirect or overwrite this restoration.
            const HRESULT status=endpoint->SetMasterVolumeLevelScalar(originalScalar,nullptr);
            lastRestoreStatus=status;
            if (FAILED(status)) {WriteVolumeReceipt("restore-write-failed");return ActionError(status);}
            int stable=0;
            for(int retry=0;retry<20;++retry){
                ::Sleep(10);
                float actual=0;
                lastRestoredScalar.reset();
                lastRestoreStatus=endpoint->GetMasterVolumeLevelScalar(&actual);
                if(FAILED(lastRestoreStatus)){WriteVolumeReceipt("restore-read-failed");return ActionError(lastRestoreStatus);}
                if(std::isfinite(actual))lastRestoredScalar=actual;
                if(std::abs(actual-originalScalar)<0.000001f)++stable;
                else {
                    stable=0;
                    const HRESULT repeated=endpoint->SetMasterVolumeLevelScalar(originalScalar,nullptr);
                    lastRestoreStatus=repeated;
                    if(FAILED(repeated)){WriteVolumeReceipt("restore-retry-failed");return ActionError(repeated);}
                }
                restoredStableSamples=stable;
                if(stable>=3){
                    volumeChanged=false;volumePending=false;restorationVerified=true;
                    WriteVolumeReceipt("restored");return Ok();
                }
            }
            // Keep cleanup armed until the original scalar is actually verified as stable.
            WriteVolumeReceipt("restoration-uncertain");
            return MakeError(ErrorCode::Timeout,"the retained audio endpoint restoration is uncertain");
        }catch(...){WriteVolumeReceipt("restoration-exception");return MakeError(ErrorCode::Internal,"the original volume could not be restored");}
    }
    void ClosePanels() noexcept {
        if(ownsMenu&&menuOwner)(void)::PostMessageW(menuOwner,WM_CANCELMODE,0,0);
        if(ownsSearch)if(const HWND window=OwnedWindow(process,L"AetherShell.Search")){
            (void)::PostMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);
            (void)::PostMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);
        }
        if(ownsQuick)if(const HWND window=OwnedWindow(process,L"AetherShell.ControlCenter")) (void)::PostMessageW(window,WM_CLOSE,0,0);
        if(ownsSettings)if(const HWND window=OwnedWindow(process,L"AetherShell.Settings")) (void)::PostMessageW(window,WM_CLOSE,0,0);
        ownsQuick=false;ownsSettings=false;ownsMenu=false;ownsSearch=false;menuOwner=nullptr;
    }
    Result<void> SelectPage(int page) {
        constexpr std::array<const wchar_t*,8> pages{L"Dock",L"Top Bar",L"Weather",L"Updates",L"General",L"About",L"Desktop",L"Appearance"};
        if(page<0||page>=static_cast<int>(pages.size()))return MakeError(ErrorCode::InvalidArgument,"settings page is invalid");
        auto item=Find(OwnedWindow(process,L"AetherShell.Settings"),UIA_NamePropertyId,pages[static_cast<std::size_t>(page)]);
        if(!item)return item.error();
        ComPtr<IUIAutomationSelectionItemPattern> select;
        HRESULT status=(*item)->GetCurrentPatternAs(UIA_SelectionItemPatternId,IID_PPV_ARGS(&select));
        if(SUCCEEDED(status))status=select->Select();
        return SUCCEEDED(status)?Ok():Result<void>(ActionError(status));
    }
};
NativePresentationActions::NativePresentationActions() noexcept=default;
NativePresentationActions::NativePresentationActions(std::unique_ptr<Impl> impl) noexcept:impl_(std::move(impl)){}
NativePresentationActions::~NativePresentationActions(){(void)Restore();}
NativePresentationActions::NativePresentationActions(NativePresentationActions&&) noexcept=default;
NativePresentationActions& NativePresentationActions::operator=(NativePresentationActions&& other) noexcept {
    if(this!=&other){(void)Restore();impl_=std::move(other.impl_);}return *this;
}
Result<NativePresentationActions> NativePresentationActions::Create(HWND dock,HWND topbar,std::wstring verifiedExplorerPinName) noexcept {
    try {
        auto impl=std::make_unique<Impl>();
        DWORD topbarProcess=0;
        (void)::GetWindowThreadProcessId(dock,&impl->process);
        (void)::GetWindowThreadProcessId(topbar,&topbarProcess);
        if(!dock||!topbar||!impl->process||topbarProcess!=impl->process)return ActionError(E_ACCESSDENIED);
        std::array<wchar_t,64> dockClass{},topbarClass{};
        (void)::GetClassNameW(dock,dockClass.data(),static_cast<int>(dockClass.size()));
        (void)::GetClassNameW(topbar,topbarClass.data(),static_cast<int>(topbarClass.size()));
        if(std::wstring_view(dockClass.data())!=L"AetherShell.Dock"||
           std::wstring_view(topbarClass.data())!=L"AetherShell.TopBar")return ActionError(E_ACCESSDENIED);
        impl->dock=dock;impl->topbar=topbar;
        impl->explorerPinName=std::move(verifiedExplorerPinName);
        HRESULT status=::CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&impl->automation));
        if(FAILED(status))return ActionError(status);
        ComPtr<IUIAutomation2> bounded;
        if(SUCCEEDED(impl->automation.As(&bounded))){(void)bounded->put_ConnectionTimeout(500);(void)bounded->put_TransactionTimeout(500);}
        return NativePresentationActions(std::move(impl));
    }catch(...){return MakeError(ErrorCode::Internal,"presentation controls could not initialize");}
}
Result<void> NativePresentationActions::Prepare(bool changesVolume) {
    if(!impl_)return ActionError(E_UNEXPECTED);
    // A clean run must not inherit an already open editor or panel with pending owner changes.
    if(OwnedWindow(impl_->process,L"AetherShell.Settings")||OwnedWindow(impl_->process,L"AetherShell.ControlCenter")||
       OwnedWindow(impl_->process,L"#32768")||OwnedWindow(impl_->process,L"AetherShell.Search"))
        return MakeError(ErrorCode::InvalidState,"close existing Aether panels before starting a presentation");
    if(!changesVolume)return Ok();
    ComPtr<IMMDevice> device;
    HRESULT status=::CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&impl_->devices));
    // Match AudioProvider's eConsole policy, rather than changing a different role's endpoint.
    if(SUCCEEDED(status))status=impl_->devices->GetDefaultAudioEndpoint(eRender,eConsole,&device);
    struct ScopedIdentifier final { wchar_t* value=nullptr;~ScopedIdentifier(){if(value)::CoTaskMemFree(value);} } identifier;
    if(SUCCEEDED(status))status=device->GetId(&identifier.value);
    if(SUCCEEDED(status)&&identifier.value)impl_->endpointId=identifier.value;
    if(SUCCEEDED(status))status=device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_INPROC_SERVER,nullptr,
        reinterpret_cast<void**>(impl_->endpoint.GetAddressOf()));
    if(SUCCEEDED(status))status=impl_->endpoint->GetMasterVolumeLevelScalar(&impl_->originalScalar);
    if(FAILED(status))return ActionError(status);
    if(!std::isfinite(impl_->originalScalar)||impl_->originalScalar<0||impl_->originalScalar>1)
        return MakeError(ErrorCode::Unavailable,"the original volume could not be measured reliably");
    impl_->originalRange=std::round(impl_->originalScalar*100);
    impl_->volumePrepared=true;
    impl_->volumePreparedTick=::GetTickCount64();
    impl_->volumeTouched=false;impl_->restorationVerified=false;impl_->restoredStableSamples=0;
    impl_->lastTarget.reset();impl_->lastEndpointScalar=impl_->originalScalar;impl_->lastDisplayedRange.reset();
    impl_->lastRestoredScalar.reset();impl_->lastRestoreStatus=E_PENDING;
    impl_->lastEndpointStatus=status;impl_->lastDisplayStatus=E_PENDING;
    impl_->WriteVolumeReceipt("prepared");
    return Ok();
}
Result<void> NativePresentationActions::Apply(const PresentationAction& action) {
    if(!impl_)return ActionError(E_UNEXPECTED);
    switch(action.kind){
    case PresentationActionKind::OpenQuickControls:return impl_->OpenQuick();
    case PresentationActionKind::OpenSettings:{
        if(impl_->ownsSettings && OwnedWindow(impl_->process,L"AetherShell.Settings"))return impl_->SelectPage(7);
        impl_->ownsMenu=true;impl_->menuOwner=impl_->topbar;
        auto menu=impl_->Invoke(impl_->topbar,UIA_AutomationIdPropertyId,L"item-1");
        if(!menu)return menu;
        HWND menuWindow=nullptr;
        for(int retry=0;retry<40&&!menuWindow;++retry){menuWindow=OwnedWindow(impl_->process,L"#32768");if(!menuWindow)::Sleep(25);}
        auto settings=impl_->Invoke(menuWindow,UIA_NamePropertyId,L"Settings…");
        if(!settings)return settings;
        impl_->ownsSettings=true;
        HWND window=nullptr;
        for(int retry=0;retry<40&&!window;++retry){window=OwnedWindow(impl_->process,L"AetherShell.Settings");if(!window)::Sleep(25);}
        return impl_->SelectPage(7);
    }
    case PresentationActionKind::VolumeOffset:{
        if(!impl_->volumePrepared)return MakeError(ErrorCode::AccessDenied,"volume changes were not authorized");
        return impl_->SetVolume(impl_->originalRange+action.value);
    }
    case PresentationActionKind::RestoreVolume:return impl_->RestoreVolume();
    case PresentationActionKind::ClosePanels:{auto restored=impl_->RestoreVolume();if(!restored)return restored;impl_->ClosePanels();return Ok();}
    case PresentationActionKind::SettingsPage:return impl_->SelectPage(static_cast<int>(action.value));
    case PresentationActionKind::QuickControlsNextPage:return impl_->Invoke(OwnedWindow(impl_->process,L"AetherShell.ControlCenter"),UIA_AutomationIdPropertyId,L"item-600");
    case PresentationActionKind::OpenSearchExplorer:return impl_->SearchExplorer();
    case PresentationActionKind::DockExplorerMenu:return impl_->ExplorerMenu();
    case PresentationActionKind::TopBarMenu:{
        impl_->ownsMenu=true;impl_->menuOwner=impl_->topbar;
        auto opened=impl_->Invoke(impl_->topbar,UIA_AutomationIdPropertyId,L"item-1");
        return opened?impl_->WaitForMenu():opened;
    }
    }
    return MakeError(ErrorCode::InvalidArgument,"presentation action is unsupported");
}
Result<void> NativePresentationActions::Restore() noexcept {
    if(!impl_)return Ok();
    auto restored=impl_->RestoreVolume();
    impl_->ClosePanels();
    return restored;
}
Result<void> NativePresentationActions::ValidateVisibleControls() noexcept {
    if(!impl_)return ActionError(E_UNEXPECTED);
    if(auto search=impl_->ValidateSearch();!search)return search;
    const HWND center=OwnedWindow(impl_->process,L"AetherShell.ControlCenter");
    if(!center)return Ok();
    try {
        auto session=impl_->Find(center,UIA_AutomationIdPropertyId,L"item-403");
        if(!session)return session.error();
        BSTR sessionState=nullptr;
        const HRESULT sessionStatus=(*session)->get_CurrentHelpText(&sessionState);
        const bool noSession=SUCCEEDED(sessionStatus)&&sessionState&&std::wstring_view(sessionState)==L"No media session";
        if(sessionState)::SysFreeString(sessionState);
        if(!noSession)return MakeError(ErrorCode::AccessDenied,"the controls presentation requires an explicitly empty media session");
        // The model-backed session assertion above is essential: disabled transport controls alone
        // do not prove absence of a title/artist. These extra capability checks are conservative.
        for(const auto* id:{L"item-400",L"item-401",L"item-402",L"item-403"}){
            auto control=impl_->Find(center,UIA_AutomationIdPropertyId,id);
            if(!control)return control.error();
            BOOL enabled=TRUE;
            if(FAILED((*control)->get_CurrentIsEnabled(&enabled))||enabled)
                return MakeError(ErrorCode::AccessDenied,"active media must be closed before the controls presentation");
        }
        return Ok();
    }catch(...){return MakeError(ErrorCode::Internal,"presentation privacy preflight failed");}
}
}
