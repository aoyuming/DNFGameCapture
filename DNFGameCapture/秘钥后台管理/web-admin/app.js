const state = {
    pending: [],
    filtered: [],
    selected: new Set(),
    activeKey: '',
    publicEntries: [],
    activePublicMain: '',
    preferredPublicMain: ''
};

const el = (id) => document.getElementById(id);

function formatTime(sec) {
    if (!sec) return '未知';
    return new Date(sec * 1000).toLocaleString('zh-CN', { hour12: false });
}

function toast(message) {
    const node = el('toast');
    node.textContent = message;
    node.classList.add('show');
    clearTimeout(node._timer);
    node._timer = setTimeout(() => node.classList.remove('show'), 3600);
}

async function api(path, options = {}) {
    const res = await fetch(path, {
        headers: { 'Content-Type': 'application/json' },
        ...options
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || '请求失败');
    return data;
}

function matchesPending(row) {
    const q = el('pending-search').value.trim();
    const mode = el('conflict-filter').value;

    if (mode === 'clean' && (row.duplicateHintCount || 0) > 0) return false;
    if (mode === 'conflict' && (row.duplicateHintCount || 0) === 0) return false;
    if (!q) return true;

    const text = [
        row.id,
        row.submitter,
        row.sample,
        ...row.entries.map(item => item.mainName),
        ...row.entries.flatMap(item => item.aliases)
    ].join('\n');
    return text.includes(q);
}

function selectedRows() {
    const map = new Map(state.pending.map(row => [row.key, row]));
    return Array.from(state.selected).map(key => map.get(key)).filter(Boolean);
}

function reviewTargetKeys() {
    if (state.selected.size > 0) return Array.from(state.selected);
    return state.activeKey ? [state.activeKey] : [];
}

function renderStats(stats, emptyCount) {
    el('stat-pending').textContent = String(stats.pendingCount || 0);
    el('stat-conflict').textContent = String(stats.duplicateHintCount || stats.conflictCount || 0);
    el('stat-pending-alias').textContent = String(stats.pendingAliasCount || 0);
    el('stat-public').textContent = `${stats.publicMainCount || 0} / ${stats.publicAliasCount || 0}`;
    el('hidden-empty-text').textContent = emptyCount > 0 ? `已隐藏 ${emptyCount} 条空记录` : '';
    el('status-text').textContent = `公共库版本 ${stats.publicVersion || 0}，更新于 ${formatTime(stats.publicUpdatedAt)}`;
}

function renderPendingList() {
    state.filtered = state.pending.filter(matchesPending);
    const list = el('pending-list');
    list.innerHTML = '';

    if (state.filtered.length === 0) {
        list.innerHTML = '<div class="detail-empty">没有符合条件的待审核记录。</div>';
        renderSelection();
        return;
    }

    state.filtered.forEach(row => {
        const item = document.createElement('div');
        item.className = `pending-item ${(row.duplicateHintCount || 0) > 0 ? 'conflict' : ''} ${row.key === state.activeKey ? 'active' : ''}`;
        item.dataset.key = row.key;
        item.innerHTML = `
            <input type="checkbox" ${state.selected.has(row.key) ? 'checked' : ''}>
            <div>
                <div class="pending-title">${escapeHtml(row.sample)}</div>
                <div class="pending-sub">${escapeHtml(row.id)} · ${formatTime(row.updatedAt)} · 提交人数 ${row.sourceCount}</div>
            </div>
            <div class="pending-badges">
                <span class="badge ok">${row.mainCount} 主号</span>
                <span class="badge">${row.pairCount} 小号</span>
                ${(row.duplicateHintCount || 0) > 0 ? `<span class="badge warn">${row.duplicateHintCount} 重复提示</span>` : ''}
            </div>
        `;

        item.addEventListener('click', (e) => {
            if (e.target.tagName === 'INPUT') return;
            state.activeKey = row.key;
            renderPendingList();
            renderDetail(row);
            focusPublicMainFromPending(row).catch(err => toast(err.message));
        });

        item.querySelector('input').addEventListener('change', (e) => {
            if (e.target.checked) state.selected.add(row.key);
            else state.selected.delete(row.key);
            renderSelection();
        });

        list.appendChild(item);
    });

    renderSelection();
}

function renderSelection() {
    const rows = selectedRows();
    const hintCount = rows.filter(row => (row.duplicateHintCount || 0) > 0).length;
    const aliasCount = rows.reduce((sum, row) => sum + row.pairCount, 0);
    el('selected-count').textContent = String(rows.length);
    el('selected-info').textContent = rows.length > 0
        ? `包含 ${aliasCount} 个小号，${hintCount} 条重复提示记录`
        : (state.activeKey ? '未勾选时将处理当前详情记录' : '');
    el('check-all').checked = state.filtered.length > 0 && state.filtered.every(row => state.selected.has(row.key));
}

function renderDetail(row) {
    const detail = el('detail-view');
    if (!row) {
        detail.className = 'detail-empty';
        detail.textContent = '点击左侧记录，查看主号、小号与重复提示。';
        el('detail-meta').textContent = '选择一条记录查看';
        return;
    }

    detail.className = 'detail-content';
    el('detail-meta').textContent = `${row.mainCount} 个主号 / ${row.pairCount} 个小号 / ${row.duplicateHintCount || 0} 条重复提示`;

    const duplicateHints = row.duplicateHints || row.conflicts || [];
    const conflictHtml = duplicateHints.length > 0
        ? `<div class="section-title">重复小号提示</div>${duplicateHints.map(c => `
            <div class="conflict-row">${escapeHtml(c.aliasName)}${c.aliasId ? `（ID: ${escapeHtml(c.aliasId)}）` : ''}：公共库里也属于 ${escapeHtml((c.owners || [c.currentOwner]).filter(Boolean).join(' / '))}${Array.isArray(c.matchedAliases) && c.matchedAliases.length ? `，命中 ${escapeHtml(c.matchedAliases.join(' / '))}` : ''}，本次提交给 ${escapeHtml(c.requestedOwner)}</div>
        `).join('')}`
        : '<div class="section-title">重复小号提示</div><div class="detail-empty">没有跨主号重复提示，可以直接通过。</div>';
    const diff = row.diff || {};
    const addedAliases = diff.addedAliases || [];
    const removedAliases = diff.removedAliases || [];
    const diffHtml = addedAliases.length > 0 || removedAliases.length > 0
        ? `<div class="section-title">差异</div>
            <div class="diff-actions">
                <button class="primary" data-review-action="approve">通过</button>
                <button class="primary" data-review-action="approve_added" ${addedAliases.length === 0 ? 'disabled' : ''}>只通过新增</button>
                <button class="danger" data-review-action="approve_removed" ${removedAliases.length === 0 ? 'disabled' : ''}>只通过删除</button>
                <button class="quiet" data-review-action="reject">驳回</button>
            </div>
            <div class="alias-row">
                <b>新增小号</b>
                <div class="alias-tags">
                    ${addedAliases.length > 0 ? addedAliases.map(alias => `<span class="alias-tag">${escapeHtml(alias)}</span>`).join('') : '<span class="alias-tag">无</span>'}
                </div>
            </div>
            <div class="alias-row">
                <b>删除小号</b>
                <div class="alias-tags">
                    ${removedAliases.length > 0 ? removedAliases.map(alias => `<span class="alias-tag">${escapeHtml(alias)}</span>`).join('') : '<span class="alias-tag">无</span>'}
                </div>
            </div>`
        : '';

    detail.innerHTML = `
        ${conflictHtml}
        ${diffHtml}
        <div class="section-title">内容</div>
        ${row.entries.map(item => `
            <div class="alias-row">
                <b>${escapeHtml(item.mainName)}</b>
                <div class="alias-tags">
                    ${item.aliases.length > 0 ? item.aliases.map(alias => `<span class="alias-tag">${escapeHtml(alias)}</span>`).join('') : '<span class="alias-tag">审核通过后删除该主号</span>'}
                </div>
            </div>
        `).join('')}
    `;

    detail.querySelectorAll('[data-review-action]').forEach(button => {
        button.addEventListener('click', () => reviewCurrent(button.dataset.reviewAction).catch(err => toast(err.message)));
    });
}

function renderPublic(data) {
    el('public-meta').textContent = `版本 ${data.version || 0} · ${data.totalMainCount || 0} 主号 / ${data.totalAliasCount || 0} 小号`;
    state.publicEntries = data.entries || [];

    const mainList = el('public-main-list');
    mainList.innerHTML = '';

    if (state.preferredPublicMain) {
        state.activePublicMain = state.publicEntries.some(item => item.mainName === state.preferredPublicMain)
            ? state.preferredPublicMain
            : '';
    } else if (state.activePublicMain && !state.publicEntries.some(item => item.mainName === state.activePublicMain)) {
        state.activePublicMain = '';
    }

    if (!state.preferredPublicMain && !state.activePublicMain && state.publicEntries.length > 0) {
        state.activePublicMain = state.publicEntries[0].mainName;
    }

    state.publicEntries.forEach(item => {
        const option = document.createElement('option');
        option.value = item.mainName;
        option.textContent = `${item.mainName} (${item.aliases.length})`;
        option.selected = item.mainName === state.activePublicMain;
        mainList.appendChild(option);
    });

    renderPublicEntry(findPublicEntry(state.activePublicMain));
}

function findPublicEntry(mainName) {
    return state.publicEntries.find(item => item.mainName === mainName) || null;
}

function renderPublicEntry(entry) {
    const aliasList = el('public-alias-list');
    aliasList.innerHTML = '';

    if (!entry) {
        el('public-main-name').value = '';
        el('public-aliases').value = '';
        return;
    }

    el('public-main-name').value = entry.mainName;
    el('public-aliases').value = entry.aliases.join('\n');

    entry.aliases.forEach(alias => {
        const option = document.createElement('option');
        option.value = alias;
        option.textContent = alias;
        aliasList.appendChild(option);
    });
}

function parseAliasText(value) {
    const out = [];
    const seen = new Set();
    const parts = String(value || '').split(/[\r\n()（）、,，;；]+/);

    for (const part of parts) {
        const alias = part.trim();
        if (!alias || seen.has(alias)) continue;
        seen.add(alias);
        out.push(alias);
    }

    return out;
}

function getPublicPayload() {
    return {
        mainName: el('public-main-name').value.trim(),
        aliases: parseAliasText(el('public-aliases').value)
    };
}

function clearPublicForm() {
    state.activePublicMain = '';
    el('public-main-list').value = '';
    renderPublicEntry(null);
}

function publicToast(data) {
    toast(data.message);
}

function escapeHtml(value) {
    return String(value || '').replace(/[&<>"']/g, ch => ({
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#39;'
    }[ch]));
}

async function loadDashboard() {
    el('status-text').textContent = '正在刷新...';
    const data = await api('/api/dashboard');
    state.pending = data.pending || [];
    state.selected = new Set(Array.from(state.selected).filter(key => state.pending.some(row => row.key === key)));
    renderStats(data.stats || {}, data.emptyCount || 0);
    renderPendingList();
    if (state.activeKey) renderDetail(state.pending.find(row => row.key === state.activeKey));
    await loadPublic();
}

async function loadPublic() {
    const q = encodeURIComponent(el('public-search').value.trim());
    const data = await api(`/api/public?limit=5000&search=${q}`);
    renderPublic(data);
}

async function focusPublicMainFromPending(row) {
    const mainName = row?.entries?.[0]?.mainName || '';
    if (!mainName) return;

    state.preferredPublicMain = mainName;
    state.activePublicMain = mainName;
    el('public-search').value = mainName;
    await loadPublic();

    if (!findPublicEntry(mainName)) {
        toast(`公共库未找到主号：${mainName}`);
    }
}

async function addPublicEntry() {
    const payload = getPublicPayload();
    const data = await api('/api/public', {
        method: 'POST',
        body: JSON.stringify(payload)
    });

    state.activePublicMain = data.entry?.mainName || payload.mainName;
    state.preferredPublicMain = '';
    el('public-search').value = '';
    publicToast(data);
    await loadDashboard();
}

async function savePublicEntry() {
    const oldMainName = state.activePublicMain;
    if (!oldMainName) {
        toast('请先在主号列表中选择要修改的记录。');
        return;
    }

    const payload = getPublicPayload();
    const data = await api(`/api/public/${encodeURIComponent(oldMainName)}`, {
        method: 'PUT',
        body: JSON.stringify(payload)
    });

    state.activePublicMain = data.entry?.mainName || payload.mainName;
    state.preferredPublicMain = '';
    el('public-search').value = '';
    publicToast(data);
    await loadDashboard();
}

async function deletePublicEntry() {
    const selectedNames = Array.from(el('public-main-list').selectedOptions).map(option => option.value).filter(Boolean);
    if (selectedNames.length === 0) {
        toast('请先在主号列表中选择要删除的记录。');
        return;
    }

    const preview = selectedNames.slice(0, 6).join('、');
    const suffix = selectedNames.length > 6 ? ` 等 ${selectedNames.length} 个` : '';
    if (!confirm(`确认删除公共库记录「${preview}${suffix}」？`)) return;

    const data = await api('/api/public/batch-delete', {
        method: 'POST',
        body: JSON.stringify({ mainNames: selectedNames })
    });

    state.activePublicMain = '';
    state.preferredPublicMain = '';
    el('public-search').value = '';
    toast(`${data.message}，共 ${data.deletedAliasCount} 个小号`);
    await loadDashboard();
}

async function importPublicEntries() {
    const text = el('public-import-text').value.trim();
    if (!text) {
        toast('请先粘贴要导入的主号小号文本。');
        return;
    }

    const data = await api('/api/public/import', {
        method: 'POST',
        body: JSON.stringify({ text })
    });

    el('public-import-text').value = '';
    state.preferredPublicMain = '';
    toast(data.errors?.length ? `${data.message}，${data.errors.length} 行未导入` : data.message);
    await loadDashboard();
}

function reviewActionLabel(action) {
    return {
        approve: '完整通过',
        approve_added: '只通过新增',
        approve_removed: '只通过删除',
        reject: '驳回'
    }[action] || '处理';
}

async function review(action, explicitKeys = null) {
    const keys = explicitKeys || reviewTargetKeys();
    if (keys.length === 0) {
        toast('请先勾选记录，或点击一条记录查看详情。');
        return;
    }

    let reason = '';
    if (action === 'reject') {
        reason = prompt('驳回原因：', '网页批量驳回') || '网页批量驳回';
    }

    const data = await api('/api/review', {
        method: 'POST',
        body: JSON.stringify({ keys, action, reason })
    });

    const firstFailure = data.failures?.[0]?.error ? `：${data.failures[0].error}` : '';
    toast(`${reviewActionLabel(action)}完成：通过 ${data.approved}，驳回 ${data.rejected}，替换 ${data.replaced}，删除 ${data.deleted}，未变化 ${data.unchanged}，失败 ${data.failed}${firstFailure}`);
    state.selected.clear();
    state.activeKey = '';
    renderDetail(null);
    await loadDashboard();
}

async function reviewCurrent(action) {
    if (!state.activeKey) {
        toast('请先点击一条待审核记录查看详情。');
        return;
    }
    await review(action, [state.activeKey]);
}

async function cleanupEmpty() {
    if (!confirm('确认清理所有空的待审核记录？')) return;
    const data = await api('/api/cleanup-empty', { method: 'POST', body: '{}' });
    toast(`已清理 ${data.cleaned} 条空记录`);
    await loadDashboard();
}

el('btn-refresh').addEventListener('click', () => loadDashboard().catch(err => toast(err.message)));
el('btn-clean-empty').addEventListener('click', () => cleanupEmpty().catch(err => toast(err.message)));
el('pending-search').addEventListener('input', renderPendingList);
el('conflict-filter').addEventListener('change', renderPendingList);
el('public-search').addEventListener('keydown', (e) => { if (e.key === 'Enter') loadPublic().catch(err => toast(err.message)); });
el('public-search').addEventListener('input', () => {
    state.preferredPublicMain = '';
});
el('btn-public-search').addEventListener('click', () => {
    state.preferredPublicMain = '';
    loadPublic().catch(err => toast(err.message));
});
el('public-main-list').addEventListener('change', (e) => {
    state.preferredPublicMain = '';
    state.activePublicMain = Array.from(e.target.selectedOptions)[0]?.value || '';
    renderPublicEntry(findPublicEntry(state.activePublicMain));
});
el('btn-public-new').addEventListener('click', clearPublicForm);
el('btn-public-add').addEventListener('click', () => addPublicEntry().catch(err => toast(err.message)));
el('btn-public-save').addEventListener('click', () => savePublicEntry().catch(err => toast(err.message)));
el('btn-public-delete').addEventListener('click', () => deletePublicEntry().catch(err => toast(err.message)));
el('btn-public-import').addEventListener('click', () => importPublicEntries().catch(err => toast(err.message)));
el('check-all').addEventListener('change', (e) => {
    state.filtered.forEach(row => {
        if (e.target.checked) state.selected.add(row.key);
        else state.selected.delete(row.key);
    });
    renderPendingList();
});
el('btn-approve').addEventListener('click', () => review('approve').catch(err => toast(err.message)));
el('btn-approve-added').addEventListener('click', () => review('approve_added').catch(err => toast(err.message)));
el('btn-approve-removed').addEventListener('click', () => review('approve_removed').catch(err => toast(err.message)));
el('btn-reject').addEventListener('click', () => review('reject').catch(err => toast(err.message)));
el('btn-clear-selected').addEventListener('click', () => {
    state.selected.clear();
    renderPendingList();
});

loadDashboard().catch(err => {
    el('status-text').textContent = '连接失败';
    toast(err.message);
});
