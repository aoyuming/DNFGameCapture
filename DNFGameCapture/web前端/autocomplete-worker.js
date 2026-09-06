let libraryNames = [];

self.onmessage = event => {
    const message = event?.data || {};
    const type = String(message.type || '');

    if (type === 'set-names') {
        libraryNames = Array.isArray(message.names) ? message.names.slice() : [];
        return;
    }

    if (type === 'query') {
        const query = String(message.query || '').trim();
        const showAll = !!message.showAll;
        const activeNames = new Set(Array.isArray(message.activeNames) ? message.activeNames : []);
        const matches = (showAll || query) ? libraryNames.filter(name =>
            !activeNames.has(name) && (!query || name.includes(query))) : [];
        self.postMessage({
            type: 'matches',
            requestId: Number(message.requestId || 0),
            matches
        });
    }
};
