#include "../VoiceCloudCache.h"
#include <iostream>
#include <cassert>
#include <functional>
using namespace dnf_voice;
static void waitFor(const std::function<bool()>& predicate) {
    const auto end = std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while (!predicate()) { if(std::chrono::steady_clock::now()>end) throw std::runtime_error("timeout"); std::this_thread::sleep_for(std::chrono::milliseconds(30)); }
}
static void control(const std::wstring& base, const wchar_t* action) {
    URL_COMPONENTS u={}; u.dwStructSize=sizeof(u);u.dwHostNameLength=static_cast<DWORD>(-1);
    assert(WinHttpCrackUrl(base.c_str(),0,0,&u));
    const auto host=std::wstring(u.lpszHostName,u.dwHostNameLength);
    HINTERNET s=WinHttpOpen(L"voice-test",WINHTTP_ACCESS_TYPE_NO_PROXY,nullptr,nullptr,0);assert(s);
    HINTERNET c=WinHttpConnect(s,host.c_str(),u.nPort,0);assert(c);
    HINTERNET r=WinHttpOpenRequest(c,L"GET",action,nullptr,nullptr,WINHTTP_DEFAULT_ACCEPT_TYPES,0);assert(r);
    assert(WinHttpSendRequest(r,nullptr,0,nullptr,0,0,0)&&WinHttpReceiveResponse(r,nullptr));
    WinHttpCloseHandle(r);WinHttpCloseHandle(c);WinHttpCloseHandle(s);
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2 || std::wstring(argv[1]).find(L"http://127.0.0.1:")!=0) return 2;
    assert(Hash("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(!SafeId("../bad")&&!IsHash("oops"));
    const std::wstring base=argv[1]; Cache cache; cache.SetPolicy(false,2,L"");cache.SetServer(base);
    waitFor([&]{return cache.List().size()==4;});
    std::wstring first; assert(!cache.Resolve(2,"double",first));control(base,L"/test/assert-no-audio");
    cache.SetPolicy(true,2,L"");waitFor([&]{return cache.Status()["status"]=="ready";});
    assert(cache.Resolve(2,"double",first));std::wstring again;assert(cache.Resolve(2,"double",again)&&again==first);control(base,L"/test/assert-one-audio");
    // A damaged local cache is re-downloaded, not trusted just because the filename exists.
    assert(WriteAtomic(first,"broken"));assert(cache.Resolve(2,"double",again));control(base,L"/test/assert-two-audio");
    control(base,L"/test/next");cache.SetPolicy(false,2,L"");cache.SetPolicy(true,2,L"");
    waitFor([&]{return cache.Status()["revision"]==std::string(64,'b');});
    std::wstring updated;assert(cache.Resolve(2,"double",updated));assert(updated!=first);control(base,L"/test/assert-three-audio");
    cache.Stop();
    // A restart reuses the on-disk manifest and hash-addressed WAVs without downloading them again.
    Cache restart;restart.SetPolicy(true,2,L"");restart.SetServer(base);waitFor([&]{return restart.List().size()==4;});assert(restart.Resolve(2,"double",again)&&again==updated);control(base,L"/test/assert-three-audio");
    // Auto selects stable LOL ID (index 17 here), even against an older theme-default catalog.
    std::wstring lol; assert(restart.Resolve(0,"double",lol));assert(lol!=updated);assert(restart.Resolve(17,"double",again)&&again==lol);control(base,L"/test/assert-four-audio");
    control(base,L"/test/offline");restart.SetPolicy(false,2,L"");restart.SetPolicy(true,2,L"");waitFor([&]{return restart.Status()["status"]=="offline";});assert(restart.Resolve(2,"double",again)&&again==updated);
    control(base,L"/test/remove");restart.SetPolicy(true,0,L"");waitFor([&]{return restart.List().size()==2;});assert(!restart.Resolve(2,"double",again));restart.Stop();
    std::cout<<"PASS: disabled/no downloads, selected prefetch, SHA cache hits/repair, audio update, restart reuse, offline cache, removed voice. No synthesis or playback.\n";
}
