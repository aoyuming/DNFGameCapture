'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const displayFiles = [
    'DNFGameCaptureDlg.cpp',
    'DNFGameCaptureDlg.h',
    'DNFGameCaptureDlg_IdentityPatch.cpp',
    'TemporalIdentityMatcher.hpp',
    '云函数/index.js',
    'web前端/index.html',
    'web前端/main.js',
    'web前端/kill.html',
    'web前端/kill.js',
    'web前端/style.css',
    'cloud-match-server/src/admin-page.ts',
    '秘钥后台管理/admin.js',
    '秘钥后台管理/admin-web.js',
    '秘钥后台管理/web-admin/index.html',
    '秘钥后台管理/web-admin/app.js',
    '用户指南.md',
    'README.md'
];

const staleTerms = ['主号', '小号'];
const stale = [];
for (const relativePath of displayFiles) {
    const filePath = path.join(root, relativePath);
    const source = fs.readFileSync(filePath, 'utf8');
    for (const term of staleTerms) {
        if (source.includes(term)) stale.push(`${relativePath}: ${term}`);
    }
}

if (stale.length > 0) {
    throw new Error(`旧显示称呼仍存在：\n${stale.join('\n')}`);
}

console.log('Display terminology check passed.');
