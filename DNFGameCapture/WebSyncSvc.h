#pragma once
#include <afxwin.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <functional>
#include <memory>
#include "json.hpp"

class CDNFGameCaptureDlg;
class CWebScoreDlg;
using json = nlohmann::json;

class WebSyncSvc {
public:
    WebSyncSvc(CDNFGameCaptureDlg& host, CWebScoreDlg*& webDlgPtr, std::mutex& dataMutex);
    ~WebSyncSvc();

    // Web 命令路由
    void RouteWebCmd(const CString& jsonStr);
    void BroadcastState();

    // 本地小号库
    void LoadAliasDB();
    void SaveAliasDB();
    void SaveAliasDB(bool mergeActivePlayers);
    std::string BuildAliasDbJsonPayload(int& mainCount, int& pairCount) const;
    std::string FilterAliasDbPayloadForReview(const std::string& payload, int& mainCount, int& pairCount, int& nakedCount) const;
    void LoadAliasCloudDeleteBaseline();
    void SaveAliasCloudDeleteBaseline() const;
    void SetAliasCloudDeleteBaselineFromPublicPlayers(const json& players);
    json BuildAliasCloudDeleteScopeJson() const;
    void ResetAliasDbCloudBaseline();

    // 云同步
    CString SubmitAliasDbForReview(const std::string& aliasDbPayload, int mainCount, int pairCount);
    CString DirectSyncAliasDbToCloud(const std::string& aliasDbPayload, int mainCount, int pairCount);
    CString SyncAliasDbFromCloud();
    void AutoSubmitAliasDbIfDirty();
    CString SubmitAliasDbSnapshotIfDirty(bool saveBeforeBuild = true);

    // 小号数据访问器（替代原 m_aliasDB 直接访问）
    CString GetAliases(const CString& mainName) const;
    void SetAliases(const CString& mainName, const CString& aliases);
    bool HasMain(const CString& mainName) const;
    void EraseMain(const CString& mainName);
    bool IsAliasDirectMode() const { return m_bAliasDirectMode; }

private:
    void InstallCmdHandlers();
    void SyncPlayersFromWeb(const json& j);

    CDNFGameCaptureDlg& m_host;
    CWebScoreDlg*& m_webDlgPtr;
    std::mutex& m_dataMutex;

    std::map<CString, CString> m_aliasDB;
    std::vector<CString> m_aliasDbPendingDeleteMains;
    std::vector<CString> m_aliasCloudDeleteBaselineMains;
    std::map<CString, CString> m_aliasCloudBaselinePlayers;
    std::string m_aliasDbCloudBaselinePayload;
    std::string m_aliasDbLastSubmittedPayload;
    bool m_bAliasDirectMode = false;

    std::map<std::string, std::function<void(const json&)>> m_cmdTable;
};