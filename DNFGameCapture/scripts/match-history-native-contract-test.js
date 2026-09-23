const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const header = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.h'), 'utf8');
const cpp = fs.readFileSync(path.join(root, 'DNFGameCaptureDlg.cpp'), 'utf8');
const main = fs.readFileSync(path.join(root, 'web前端', 'main.js'), 'utf8');
const html = fs.readFileSync(path.join(root, 'web前端', 'index.html'), 'utf8');

assert.match(header, /struct MatchHistoryEntry\s*\{/,
    '原生必须定义比赛历史条目');
assert.match(header, /m_matchHistoryMaxSteps\s*=\s*30/,
    '默认历史步数必须为 30');
assert.match(header, /m_matchHistoryCursor/,
    '原生必须保存历史游标');
assert.match(header, /UndoMatchHistory\(/,
    '原生必须提供撤销入口');
assert.match(header, /RedoMatchHistory\(/,
    '原生必须提供重做入口');
assert.match(header, /RestoreMatchHistoryToEntry\(std::uint64_t entryId,\s*CString& errorMessage\)/,
    '原生必须提供按历史条目直接回溯的入口');
assert.match(cpp, /BuildMatchHistorySnapshotPayloadUnlocked\(/,
    '历史快照必须覆盖场上完整状态');
assert.match(cpp, /currentStreak/,
    '历史快照必须覆盖八名选手的连胜状态');
assert.match(cpp, /historyRecentEvents[\s\S]*ocrSummary[\s\S]*candidateSummary[\s\S]*algorithmName[\s\S]*snapshotPath/,
    '历史快照必须保留识别记录摘要');
assert.match(cpp, /event\.ocrSummary[\s\S]*event\.candidateSummary[\s\S]*event\.algorithmName[\s\S]*event\.snapshotPath/,
    '历史恢复必须还原识别记录摘要');
assert.doesNotMatch(cpp, /historyActiveAliasDb/,
    '对局历史不得包含永久选手库，否则撤销会拆除别名关联');
assert.doesNotMatch(header + cpp, /RestoreMatchHistoryActiveAliasDb/,
    '对局历史不得恢复或改写永久选手库');
assert.match(cpp, /MatchHistoryApplyGuard/,
    '历史恢复必须在异常时清理应用中状态');
assert.match(cpp, /RecordMatchHistoryTransition\(/,
    '状态变动后必须写入历史栈');

const startupStart = cpp.indexOf('void CDNFGameCaptureDlg::RunStartupStage(int stage)');
const startupStageZeroEnd = cpp.indexOf('case 1:', startupStart);
assert.ok(startupStart >= 0 && startupStageZeroEnd > startupStart,
    '必须能定位启动阶段的历史基线逻辑');
const startupStageZeroBody = cpp.slice(startupStart, startupStageZeroEnd);
assert.match(startupStageZeroBody,
    /StartPlayerLibrary\(\);[\s\S]*?if \(!m_playerLibraryStore\)\s*\{\s*InitializeMatchHistory\(\);\s*\}/,
    '选手库后台启动失败时才能在启动阶段直接建立历史基线');

const libraryStart = cpp.indexOf('void CDNFGameCaptureDlg::StartPlayerLibrary()');
const libraryEnd = cpp.indexOf('void CDNFGameCaptureDlg::PublishPlayerLibrary(', libraryStart);
assert.ok(libraryStart >= 0 && libraryEnd > libraryStart,
    '必须能定位选手库首次加载回调');
const libraryStartBody = cpp.slice(libraryStart, libraryEnd);
assert.match(libraryStartBody,
    /m_playerLibraryRequests\[id\]\s*=\s*\[this\][\s\S]*?InitializeMatchHistory\(\);[\s\S]*?\};/,
    '首次选手库加载完成后才能建立历史基线，启动加载不得生成操作记录');

const observeStart = cpp.indexOf('void CDNFGameCaptureDlg::ObserveMatchHistoryState()');
const observeEnd = cpp.indexOf('bool CDNFGameCaptureDlg::ApplyMatchHistorySnapshot(', observeStart);
assert.ok(observeStart >= 0 && observeEnd > observeStart,
    '必须能定位历史状态观察逻辑');
const observeBody = cpp.slice(observeStart, observeEnd);
assert.match(observeBody,
    /if \(currentSnapshot == m_matchHistoryObservedSnapshot\) \{\s*m_matchHistoryMutationPending = false;\s*m_matchHistoryPendingLabel\.Empty\(\);\s*m_matchHistoryPendingSource\.Empty\(\);\s*return;\s*\}/,
    '无实际变化的操作必须清除待写入标签，不能污染下一条历史');
assert.match(cpp, /ApplyMatchHistorySnapshot\(/,
    '撤销和重做必须通过完整快照恢复');
assert.match(cpp, /cmd_match_history_undo/,
    'C++ 必须处理比赛历史撤销命令');
assert.match(cpp, /cmd_match_history_redo/,
    'C++ 必须处理比赛历史重做命令');
assert.match(cpp, /cmd_match_history_restore/,
    'C++ 必须处理按条目直接回溯命令');
assert.match(cpp, /RestoreMatchHistoryToEntry\([\s\S]*?entry\.afterSnapshot[\s\S]*?m_matchHistoryCursor\s*=\s*index\s*\+\s*1/,
    '直接回溯必须恢复选中操作完成后的快照并同步历史游标');
assert.doesNotMatch(header + cpp, /m_playerLibraryPreserveRosterRequests|RebaseMatchHistoryObservedAliasDb/,
    '历史撤销不再保存选手库，不应留下异步选手库保护补丁');
assert.doesNotMatch(header,
    /QueuePlayerLibrarySave\(bool mergeActivePlayers,\s*bool preserveActiveRoster\)/,
    '选手库保存不应再接收历史恢复专用参数');

const applyStart = cpp.indexOf('bool CDNFGameCaptureDlg::ApplyMatchHistorySnapshot(');
const applyEnd = cpp.indexOf('bool CDNFGameCaptureDlg::UndoMatchHistory(', applyStart);
assert.ok(applyStart >= 0 && applyEnd > applyStart,
    '必须能定位历史快照恢复逻辑');
assert.doesNotMatch(cpp.slice(applyStart, applyEnd), /m_aliasDB|SaveAliasDB|QueuePlayerLibrarySave/,
    '恢复对局快照不得读写永久选手库');

assert.match(header, /RefreshAfterMatchHistoryApply\(\)/,
    '历史恢复必须使用独立的刷新与保存路径');
const historyRefreshStart = cpp.indexOf('bool CDNFGameCaptureDlg::RefreshAfterMatchHistoryApply()');
const historyRefreshEnd = cpp.indexOf('void CDNFGameCaptureDlg::ClearTeamSyncState()', historyRefreshStart);
assert.ok(historyRefreshStart >= 0 && historyRefreshEnd > historyRefreshStart,
    '必须能定位历史恢复后的刷新逻辑');
const historyRefreshBody = cpp.slice(historyRefreshStart, historyRefreshEnd);
assert.doesNotMatch(historyRefreshBody, /SaveAliasDB|QueuePlayerLibrarySave/,
    '历史恢复后不得保存或提交永久选手库');
assert.match(historyRefreshBody, /SaveConfigToFile\(\)[\s\S]*WriteScoreToFile\(\)[\s\S]*SyncDataToTree\(\)[\s\S]*RefreshDisplay\(\)/,
    '历史恢复后仍必须保存对局配置、比分输出并刷新界面');

for (const [method, nextMethod] of [
    ['UndoMatchHistory', 'RedoMatchHistory'],
    ['RedoMatchHistory', 'RestoreMatchHistoryToEntry'],
    ['RestoreMatchHistoryToEntry', 'SetMatchHistoryMaxSteps']
]) {
    const start = cpp.indexOf(`bool CDNFGameCaptureDlg::${method}(`);
    const end = cpp.indexOf(`bool CDNFGameCaptureDlg::${nextMethod}(`, start);
    assert.ok(start >= 0 && end > start, `必须能定位 ${method} 的实现`);
    assert.match(cpp.slice(start, end), /RefreshAfterMatchHistoryApply\(\)/,
        `${method} 必须使用不修改永久选手库的刷新路径`);
}
const undoStart = cpp.indexOf('bool CDNFGameCaptureDlg::UndoMatchHistory(');
const undoEnd = cpp.indexOf('bool CDNFGameCaptureDlg::RedoMatchHistory(', undoStart);
const undoBody = cpp.slice(undoStart, undoEnd);
assert.doesNotMatch(undoBody, /m_matchHistory\.(?:erase|clear|pop_front|pop_back)\(/,
    '撤销只能移动游标，不得删除任何历史记录');

const stateStart = cpp.indexOf('nlohmann::json CDNFGameCaptureDlg::BuildMatchHistoryStateJson() const');
const stateEnd = cpp.indexOf('void CDNFGameCaptureDlg::OnMatchStateChanged(', stateStart);
assert.ok(stateStart >= 0 && stateEnd > stateStart,
    '必须能定位历史状态序列化逻辑');
const stateBody = cpp.slice(stateStart, stateEnd);
assert.match(stateBody, /index\s*<\s*m_matchHistory\.size\(\)/,
    '即使游标退到 0，也必须发送全部历史记录');
assert.match(stateBody, /"redo",\s*index\s*>=\s*m_matchHistoryCursor/,
    '游标之后的记录必须作为可重做项保留');
assert.match(cpp, /matchHistory/,
    'C++ Web 状态必须广播比赛历史摘要');
for (const label of [
    '手动修改击杀', '手动修改死亡', '手动修改人名', '手动重置战绩',
    '手动清空选手', '自动识别', '手动同步', '实时同步'
]) {
    assert.match(cpp, new RegExp(label), `历史记录必须包含明确动作标签：${label}`);
}
assert.match(cpp, /historyLabel|historySource/,
    '同步应用路径必须允许传入明确的历史动作和来源');
assert.match(cpp, /fromRealtime \? L"实时同步"[\s\S]*?L"本地状态更新"[\s\S]*?L"本机"/,
    '本地状态上传不能被标成云端同步');
assert.doesNotMatch(cpp, /云端状态变化/,
    '新版原生历史不得再使用容易误解的“云端状态变化”标签');
assert.match(main, /cmd_match_history_undo/,
    'Web 必须发送比赛历史撤销命令');
assert.match(main, /cmd_match_history_redo/,
    'Web 必须发送比赛历史重做命令');
assert.match(main, /event\.ctrlKey[\s\S]*?event\.key.*[zZ]/,
    'Web 必须绑定 Ctrl+Z');
assert.match(main, /event\.ctrlKey[\s\S]*?event\.key.*[yY]/,
    'Web 必须绑定 Ctrl+Y');
assert.match(html, /match-history-max-steps/,
    'Web 必须提供最大历史步数设置');

console.log('Match-history native contract tests passed.');
