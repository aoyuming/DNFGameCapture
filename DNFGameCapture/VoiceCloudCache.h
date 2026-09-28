#pragma once
// Public, read-only audio catalog/cache. Never calls a synthesis endpoint or holds API keys.
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <set>
#include <map>
#include <vector>
#include "json.hpp"
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
namespace dnf_voice {
using Json = nlohmann::json;
namespace fs = std::filesystem;
inline std::string Hash(const std::string& bytes) {
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0; std::string result;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return result;
    if (CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        BYTE digest[32]; DWORD size = sizeof(digest);
        if (CryptHashData(hash, reinterpret_cast<const BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()), 0) && CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0)) {
            for (BYTE b : digest) { result += "0123456789abcdef"[b >> 4]; result += "0123456789abcdef"[b & 15]; }
        }
        CryptDestroyHash(hash);
    }
    CryptReleaseContext(provider, 0); return result;
}
inline bool IsHash(const std::string& s) { return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos; }
inline bool SafeId(const std::string& s) { return !s.empty() && s.size() <= 64 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos; }
inline bool ValidCatalog(const Json& c) {
    try {
        if (!c.is_object() || c.at("schema") != 1 || !IsHash(c.at("revision").get<std::string>()) || !c.at("voices").is_array() || c.at("voices").size() > 256) return false;
        if (!c.at("phrases").is_object() || c.at("phrases").size() > 64) return false;
        for (auto it = c.at("phrases").begin(); it != c.at("phrases").end(); ++it) if (!SafeId(it.key()) || !it.value().is_string() || it.value().get<std::string>().size() > 512) return false;
        if (!SafeId(c.at("defaults").at("normal").get<std::string>()) || !SafeId(c.at("defaults").at("ink").get<std::string>())) return false;
        std::set<int> indexes; std::set<std::string> ids;
        for (const auto& v : c.at("voices")) {
            const int index = v.at("index").get<int>(); const auto id = v.at("id").get<std::string>();
            if (index < 1 || index == 5 || index > 65535 || !indexes.insert(index).second || !SafeId(id) || id == "auto" || id == "windows" || !ids.insert(id).second) return false;
            const auto label = v.at("label").get<std::string>(); if (label.empty() || label.size() > 320 || !v.at("clips").is_object() || v.at("clips").size() > 64) return false;
            for (auto it = v.at("clips").begin(); it != v.at("clips").end(); ++it) {
                const auto h = it.value().at("sha256").get<std::string>(); const auto size = it.value().at("bytes").get<int>();
                if (!SafeId(it.key()) || !IsHash(h) || size < 46 || size > 8 * 1024 * 1024 || it.value().at("url") != "/api/voice/audio/" + h + ".wav") return false;
            }
        }
        return true;
    } catch (...) { return false; }
}
inline bool Read(const fs::path& path, size_t limit, std::string& data) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate); if (!stream) return false;
    const auto size = stream.tellg(); if (size < 0 || static_cast<unsigned long long>(size) > limit) return false;
    data.resize(static_cast<size_t>(size)); stream.seekg(0); return data.empty() || !!stream.read(&data[0], static_cast<std::streamsize>(data.size()));
}
inline bool WriteAtomic(const fs::path& path, const std::string& data) {
    std::error_code ec; fs::create_directories(path.parent_path(), ec); if (ec) return false;
    const fs::path temp = path.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    { std::ofstream s(temp, std::ios::binary | std::ios::trunc); if (!s || !s.write(data.data(), static_cast<std::streamsize>(data.size())) || !s.flush()) return false; }
    if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    fs::remove(temp, ec); return false;
}
class Cache {
    struct Http { HINTERNET value; explicit Http(HINTERNET h) : value(h) {} ~Http() { if (value) WinHttpCloseHandle(value); } operator HINTERNET() const { return value; } };
    std::mutex mutex_; std::condition_variable wake_; std::thread thread_;
    bool stop_ = false, changed_ = false, active_ = false; int selection_ = 0;
    std::wstring server_, loadedServer_, bundled_; fs::path root_;
    Json catalog_; std::string status_ = "waiting";
    std::set<std::pair<int,std::string>> wanted_;
    std::chrono::steady_clock::time_point nextPoll_{};
    bool Current(const std::wstring& base, bool audio) { std::lock_guard<std::mutex> lock(mutex_); return !stop_ && server_ == base && (!audio || active_); }
    bool Get(const std::wstring& base, const std::wstring& route, const std::string& etag, size_t limit, bool audio, DWORD& status, std::string& body) {
        if (!Current(base, audio)) return false;
        URL_COMPONENTS u = {}; u.dwStructSize = sizeof(u); u.dwHostNameLength = u.dwUrlPathLength = u.dwExtraInfoLength = u.dwUserNameLength = u.dwPasswordLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(base.c_str(), 0, 0, &u) || (u.nScheme != INTERNET_SCHEME_HTTPS && u.nScheme != INTERNET_SCHEME_HTTP) || u.dwExtraInfoLength || u.dwUserNameLength || u.dwPasswordLength || !u.dwHostNameLength) return false;
        std::wstring host(u.lpszHostName, u.dwHostNameLength), path = u.dwUrlPathLength ? std::wstring(u.lpszUrlPath, u.dwUrlPathLength) : L"";
        while (!path.empty() && path.back() == L'/') path.pop_back(); path += route;
        Http session(WinHttpOpen(L"DNF VoiceCache/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0)); if (!session.value) return false;
        WinHttpSetTimeouts(session, 4000, 4000, 4000, 6000);
        Http connection(WinHttpConnect(session, host.c_str(), u.nPort, 0)); if (!connection.value) return false;
        Http request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, nullptr, WINHTTP_DEFAULT_ACCEPT_TYPES, u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)); if (!request.value) return false;
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER; WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
        std::wstring headers = L"Accept: application/json, audio/wav\r\n";
        if (!etag.empty()) headers += L"If-None-Match: \"" + std::wstring(etag.begin(), etag.end()) + L"\"\r\n";
        if (!WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()), nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) return false;
        DWORD size = sizeof(status); if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr)) return false;
        if (status == 304) return true; if (status != 200) return false;
        char buffer[16384]; DWORD count = 0; const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (Current(base,audio) && std::chrono::steady_clock::now() < deadline) {
            if (!WinHttpReadData(request, buffer, sizeof(buffer), &count)) return false;
            if (!count) return true; if (body.size() + count > limit) return false; body.append(buffer, count);
        }
        return false;
    }
    static Json Clip(const Json& catalog, int index, const std::string& key, std::string* id = nullptr) {
        if (!ValidCatalog(catalog) || index == 5) return Json();
        const bool ink = key.compare(0,4,"ink-") == 0 || key == "ak-ink";
        // Stable ID, not an assumed numeric slot (existing custom voices can occupy index 6).
        const std::string defaultId = index == 0 ? "lol-announcer" : catalog["defaults"][ink ? "ink" : "normal"].get<std::string>();
        bool found = false; for (const auto& v : catalog["voices"]) if (v["index"] == index) found = true;
        // Removed/disabled choices fall back to the server's theme default, never another array position.
        for (const auto& v : catalog["voices"]) if (((index == 0 || !found) && v["id"] == defaultId) || (found && index != 0 && v["index"] == index)) {
            if (id) *id = v["id"].get<std::string>(); return v["clips"].value(key, Json());
        }
        return Json();
    }
    static bool ValidFile(const fs::path& path, const Json& clip) {
        if (clip.is_null()) return false; std::string bytes;
        return Read(path, 8 * 1024 * 1024, bytes) && bytes.size() == clip["bytes"].get<size_t>() && bytes.size() >= 46 && bytes.compare(0,4,"RIFF") == 0 && bytes.compare(8,4,"WAVE") == 0 && Hash(bytes) == clip["sha256"].get<std::string>();
    }
    void Loop() {
        for (;;) {
            std::wstring base, bundled; bool active; int selection; fs::path root; Json catalog; std::set<std::pair<int,std::string>> wanted;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait_until(lock, nextPoll_, [&] { return stop_ || changed_; });
                if (stop_) return; changed_ = false; base = server_; active = active_; selection = selection_; bundled = bundled_; wanted.swap(wanted_);
                nextPoll_ = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            }
            try {
                if (base.empty()) { std::lock_guard<std::mutex> lock(mutex_); catalog_ = Json(); loadedServer_.clear(); status_ = "waiting"; continue; }
                wchar_t data[MAX_PATH] = {}; const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", data, MAX_PATH); if (!size || size >= MAX_PATH) continue;
                const auto serverHash = Hash(std::string(reinterpret_cast<const char*>(base.data()), base.size()*sizeof(wchar_t))); if (serverHash.empty()) continue;
                root = fs::path(data) / L"DNFGameCapture" / L"voice-cloud" / serverHash;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (base != server_) continue;
                    if (loadedServer_ != base) {
                        std::string text; Json saved;
                        if (Read(root / L"catalog.json", 2*1024*1024, text)) saved = Json::parse(text, nullptr, false);
                        catalog_ = ValidCatalog(saved) ? saved : Json(); loadedServer_ = base; root_ = root;
                    }
                    catalog = catalog_; status_ = "syncing";
                }
                DWORD status = 0; std::string body;
                bool online = Get(base, L"/api/voice/catalog", catalog.is_null() ? "" : catalog.value("revision", ""), 2*1024*1024, false, status, body);
                if (online && status == 200) {
                    Json updated = Json::parse(body, nullptr, false);
                    if (ValidCatalog(updated) && Current(base,false)) { catalog = updated; WriteAtomic(root / L"catalog.json", body); }
                    else online = false;
                }
                { std::lock_guard<std::mutex> lock(mutex_); if (base != server_) continue; catalog_ = catalog; wake_.notify_all(); }
                bool complete = true;
                if (active && ValidCatalog(catalog)) {
                    // Prefetch just the selected voice. Auto mode needs normal + ink phrases, not every voice.
                    for (auto it = catalog["phrases"].begin(); it != catalog["phrases"].end(); ++it) wanted.insert({selection,it.key()});
                    for (const auto& item : wanted) {
                        if (!Current(base,true)) break;
                        std::string voiceId; const Json clip = Clip(catalog,item.first,item.second,&voiceId); if (clip.is_null()) { if (item.first != 5) complete = false; continue; }
                        const auto hash = clip["sha256"].get<std::string>(); const auto path = root / (hash + ".wav");
                        if (ValidFile(path,clip)) continue;
                        // Existing 84 bundled WAVs are accepted only when their SHA matches the server version.
                        const auto legacy = fs::path(bundled) / L"voice" / voiceId / (item.second + ".wav");
                        std::string wav;
                        if (ValidFile(legacy,clip)) Read(legacy,8*1024*1024,wav);
                        else { DWORD code=0; if (!Get(base,L"/api/voice/audio/" + std::wstring(hash.begin(),hash.end()) + L".wav", "", 8*1024*1024, true, code, wav)) {complete=false;break;} }
                        if (wav.size() != clip["bytes"].get<size_t>() || Hash(wav) != hash || !Current(base,true) || !WriteAtomic(path,wav)) {complete=false;break;}
                        wake_.notify_all();
                    }
                }
                { std::lock_guard<std::mutex> lock(mutex_); if (base == server_) status_ = !online ? "offline" : !active_ ? "disabled" : complete ? "ready" : "partial"; }
            } catch (...) { std::lock_guard<std::mutex> lock(mutex_); status_ = "offline"; }
            wake_.notify_all();
        }
    }
public:
    ~Cache() { Stop(); }
    void SetServer(std::wstring base) {
        std::lock_guard<std::mutex> lock(mutex_); while (!base.empty() && base.back() == L'/') base.pop_back();
        if (stop_ || base == server_) return; server_ = std::move(base); changed_ = true;
        if (!thread_.joinable()) thread_ = std::thread([this] { Loop(); }); wake_.notify_all();
    }
    void SetPolicy(bool enabled, int index, const std::wstring& bundled) {
        std::lock_guard<std::mutex> lock(mutex_); if (active_ != enabled || selection_ != index || bundled_ != bundled) { active_ = enabled; selection_ = index; bundled_ = bundled; changed_ = true; if (!enabled) wanted_.clear(); wake_.notify_all(); }
    }
    bool HasCatalog() { std::lock_guard<std::mutex> lock(mutex_); return server_ == loadedServer_ && ValidCatalog(catalog_); }
    Json List() {
        std::lock_guard<std::mutex> lock(mutex_);
        Json list = Json::array({{{"index",0},{"id","auto"},{"label",u8"默认（LOL音效播报）"}}});
        if (server_ == loadedServer_ && ValidCatalog(catalog_)) for (const auto& v : catalog_["voices"]) list.push_back({{"index",v["index"]},{"id",v["id"]},{"label",v["label"]}});
        list.push_back({{"index",5},{"id","windows"},{"label",u8"Windows 系统语音（离线）"}}); return list;
    }
    Json Status() { std::lock_guard<std::mutex> lock(mutex_); return {{"status",status_},{"revision",catalog_.is_object()?catalog_.value("revision",""):""},{"pollSeconds",60}}; }
    bool Resolve(int index, const std::string& key, std::wstring& out) {
        Json clip; fs::path path; std::wstring base;
        {
            std::unique_lock<std::mutex> lock(mutex_); if (!active_ || server_ != loadedServer_) return false;
            base = server_; clip = Clip(catalog_,index,key); if (clip.is_null()) return false; path = root_ / (clip["sha256"].get<std::string>() + ".wav");
        }
        if (ValidFile(path,clip)) {out=path.wstring();return true;}
        {
            std::unique_lock<std::mutex> lock(mutex_); if (!active_ || server_ != base || stop_) return false;
            if (wanted_.size() < 32) wanted_.insert({index,key}); changed_ = true; wake_.notify_all();
            wake_.wait_for(lock,std::chrono::seconds(10),[&] { std::error_code ec; return stop_ || !active_ || server_ != base || ValidFile(path,clip); });
            if (stop_ || !active_ || server_ != base) return false;
        }
        if (!ValidFile(path,clip)) return false; out=path.wstring();return true;
    }
    void Stop() { {std::lock_guard<std::mutex> lock(mutex_);stop_=true;active_=false;wake_.notify_all();} if(thread_.joinable())thread_.join(); }
};
}
