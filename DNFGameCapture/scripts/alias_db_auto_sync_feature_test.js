'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8');
const cpp = read('DNFGameCaptureDlg.cpp').replace(/\r\n/g, '\n');
const header = read('DNFGameCaptureDlg.h');
const cloud = read(path.join('云函数', 'index.js'));
const project = read('DNFGameCapture.vcxproj');
const filters = read('DNFGameCapture.vcxproj.filters');
const html = read(path.join('web前端', 'index.html'));
const js = read(path.join('web前端', 'main.js'));

function assertContains(text, fragment, message) {
    if (!text.includes(fragment)) throw new Error(message);
}

assertContains(header, 'CString m_aliasAutoSyncLastResult',
    'C++ 自动同步状态缺少最近结果字段');
assertContains(header, 'bool SaveAliasDbAutoSyncSettings() const;',
    '自动同步检查点保存没有返回持久化结果');
assertContains(cpp, 'data["aliasDbAutoSync"]',
    'C++ 未向 Web 暴露自动同步状态');
assertContains(cpp, 'cmd_set_alias_auto_sync',
    'C++ 未处理自动同步开关命令');
assertContains(project, 'AliasDbAutoSyncPolicy.cpp',
    '工程文件未包含自动同步策略实现');
assertContains(filters, 'AliasDbAutoSyncPolicy.h',
    '工程筛选器未包含自动同步策略头文件');
assertContains(html, 'alias-auto-sync-enabled',
    'Web 未提供自动同步开关');
assertContains(html, 'alias-auto-sync-next',
    'Web 未提供下次自动同步时间');
assertContains(js, 'function renderAliasDbAutoSync',
    'Web 未实现自动同步状态渲染');
assertContains(js, 'cmd_set_alias_auto_sync',
    'Web 未发送自动同步开关命令');
assertContains(js, 'aliasDbAutoSync.inFlight',
    'Web 未根据自动同步忙碌状态处理手动按钮');
assertContains(js, 'aliasDbAutoSync.lastPush.status === \'unsupported\'',
    'Web 未区分尚未探测能力与云函数明确不支持追加模式');
assertContains(cpp, 'm_aliasAutoSyncLastKnownAuthorized',
    '自动同步未缓存授权状态变化，无法在授权过期时刷新 Web');
assertContains(cpp, 'm_aliasAutoSyncLastPushMessage = L"等待下次周期执行";',
    '授权成功但七天周期未到时，推送状态仍会误显示为等待授权');
assertContains(cpp, 'm_aliasAutoSyncLastPullMessage = L"等待下次周期执行";',
    '授权成功但七天周期未到时，拉取状态仍会误显示为等待授权');
assertContains(cpp, '七天检查点保存失败',
    '自动同步未提示七天检查点持久化失败');
assertContains(cloud, 'mergeAppendAlias',
    '云函数追加路径未复用按 ID 合并逻辑');

const authTrigger = cpp.match(
    /if \(m_bIsAuthValid && serverUrlValid\) \{[\s\S]{0,320}?MaybeStartAliasDbAutoSync\((true|false)\);/);
if (!authTrigger || authTrigger[1] !== 'false') {
    throw new Error('授权成功后的自动同步不能绕过七天周期判断');
}

const leaseTrigger = cpp.match(
    /AppLog\(L"✅ \[云端验证\] 已使用本机加密授权租约。[\s\S]{0,180}?MaybeStartAliasDbAutoSync\((true|false)\);/);
if (!leaseTrigger || leaseTrigger[1] !== 'false') {
    throw new Error('使用五天租约启动时不能绕过七天周期判断');
}

assertContains(cpp, 'const bool localPayloadEmpty = mainCount <= 0',
    '自动同步未按主号数量识别空小号库');
assertContains(cpp, 'aliasDbBefore',
    '自动合并缺少保存失败时的小号库回滚快照');
assertContains(cpp, 'SaveAliasDB(false)',
    '自动合并保存失败后未尝试恢复本地库文件');
assertContains(cpp, 'result->lifetime = lifetime',
    '自动同步异步结果缺少所属窗口生命周期令牌');
assertContains(cpp, 'result->lifetime != m_aliasAutoSyncLifetime',
    '自动同步结果未拒绝复用 HWND 投递的旧消息');
assertContains(cpp, '(!result->appendSupported && result->pullOk)',
    '旧云函数只拉取成功时未完成七天周期记账');

const autoMerge = cpp.match(
    /bool CDNFGameCaptureDlg::MergePublicAliasDbForAutoSync\([\s\S]*?\n}\n\nvoid CDNFGameCaptureDlg::MaybeStartAliasDbAutoSync/);
if (!autoMerge) {
    throw new Error('未找到自动小号库合并实现');
}
if (autoMerge[0].includes('m_players[') ||
    autoMerge[0].includes('SaveConfigToFile()')) {
    throw new Error('自动小号库拉取只能修改 alias_db.ini，不能改当前比赛选手或比赛配置');
}

const autoResult = cpp.match(
    /LRESULT CDNFGameCaptureDlg::OnAliasDbAutoSyncResult\([\s\S]*?\n}\n\nvoid CDNFGameCaptureDlg::ResetAliasDbCloudBaseline/);
if (!autoResult) {
    throw new Error('未找到自动小号库结果处理实现');
}
assertContains(autoResult[0], '自动推送结果：',
    '自动小号库完成后未在 C++ 日志区分推送/跳过结果');
assertContains(autoResult[0], '云函数需更新，自动推送未执行',
    '旧云函数能力不支持时未在 C++ 日志明确提示');

const autoAttempt = cpp.match(
    /void CDNFGameCaptureDlg::StartAliasDbAutoSyncAttempt\([\s\S]*?\n}\n\nLRESULT CDNFGameCaptureDlg::OnAliasDbAutoSyncResult/);
if (!autoAttempt) {
    throw new Error('未找到自动小号库后台任务实现');
}
assertContains(autoAttempt[0], 'const bool useServerAuthV2 = m_cloudServerAuthV2',
    '自动同步没有读取测试服授权模式');
assertContains(autoAttempt[0], 'DnfFetchV2PublicAliasDb',
    '测试服自动同步没有调用 v2 公共库接口');
assertContains(autoAttempt[0], 'DnfSubmitV2PlayerLibrary',
    '测试服自动同步没有调用 v2 投稿接口');
assertContains(autoAttempt[0], 'serverSessionToken',
    '自动同步没有携带测试服会话令牌');
assertContains(autoAttempt[0], 'serverDeviceId',
    '自动同步没有携带测试服设备标识');

console.log('Alias DB auto-sync feature checks passed.');
