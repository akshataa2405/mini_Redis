const API_BASE = window.location.origin;

let commandHistory = [];
let historyIndex = -1;

const terminalOutput = document.getElementById('terminalOutput');
const terminalInput = document.getElementById('terminalInput');
const btnSendCmd = document.getElementById('btnSendCmd');
const btnClearTerminal = document.getElementById('btnClearTerminal');
const keysContainer = document.getElementById('keysContainer');
const statKeyCount = document.getElementById('statKeyCount');
const filterPattern = document.getElementById('filterPattern');
const btnRefreshKeys = document.getElementById('btnRefreshKeys');
const btnSampleData = document.getElementById('btnSampleData');

// Modal Elements
const newKeyModal = document.getElementById('newKeyModal');
const btnNewKeyModal = document.getElementById('btnNewKeyModal');
const btnCloseModal = document.getElementById('btnCloseModal');
const btnCancelModal = document.getElementById('btnCancelModal');
const btnSubmitKey = document.getElementById('btnSubmitKey');
const inputKeyName = document.getElementById('inputKeyName');
const inputKeyValue = document.getElementById('inputKeyValue');
const inputKeyTTL = document.getElementById('inputKeyTTL');

// Formatting RESP reply for terminal display
function formatResp(raw) {
    if (!raw) return { text: '(nil)', cls: 'val' };
    
    if (raw.startsWith('+')) {
        return { text: raw.slice(1).trim(), cls: 'ok' };
    } else if (raw.startsWith('-')) {
        return { text: '(error) ' + raw.slice(1).trim(), cls: 'err' };
    } else if (raw.startsWith(':')) {
        return { text: '(integer) ' + raw.slice(1).trim(), cls: 'num' };
    } else if (raw.startsWith('$')) {
        if (raw.startsWith('$-1')) {
            return { text: '(nil)', cls: 'val' };
        }
        const lines = raw.split('\r\n');
        return { text: `"${lines[1] || ''}"`, cls: 'val' };
    } else if (raw.startsWith('*')) {
        if (raw.startsWith('*-1')) return { text: '(nil)', cls: 'val' };
        const lines = raw.split('\r\n');
        let count = parseInt(lines[0].slice(1), 10);
        if (count === 0) return { text: '(empty list or set)', cls: 'val' };
        
        let out = [];
        let idx = 1;
        for (let i = 1; i < lines.length && out.length < count; i++) {
            if (lines[i].startsWith('$')) {
                i++;
                out.push(`${idx}) "${lines[i] || ''}"`);
                idx++;
            }
        }
        return { text: out.join('\n'), cls: 'val' };
    }
    return { text: raw.trim(), cls: 'val' };
}

async function executeCommand(cmdStr) {
    if (!cmdStr || !cmdStr.trim()) return;
    
    cmdStr = cmdStr.trim();
    commandHistory.push(cmdStr);
    historyIndex = commandHistory.length;

    // Append to terminal output
    const entry = document.createElement('div');
    entry.className = 'term-entry';
    entry.innerHTML = `
        <div class="term-cmd">
            <span class="prompt">127.0.0.1:6380&gt;</span>
            <span>${escapeHtml(cmdStr)}</span>
        </div>
        <div class="term-res">Executing...</div>
    `;
    terminalOutput.appendChild(entry);
    terminalOutput.scrollTop = terminalOutput.scrollHeight;

    try {
        const res = await fetch(`${API_BASE}/api/exec`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ command: cmdStr })
        });
        const data = await res.json();
        const formatted = formatResp(data.raw || '');
        
        const resEl = entry.querySelector('.term-res');
        resEl.className = `term-res ${formatted.cls}`;
        resEl.textContent = formatted.text;
    } catch (err) {
        const resEl = entry.querySelector('.term-res');
        resEl.className = 'term-res err';
        resEl.textContent = `(error) ${err.message}`;
    }

    terminalOutput.scrollTop = terminalOutput.scrollHeight;
    refreshKeyspace();
}

function escapeHtml(str) {
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

async function refreshKeyspace() {
    try {
        const pattern = filterPattern.value.trim() || '*';
        const res = await fetch(`${API_BASE}/api/keys?pattern=${encodeURIComponent(pattern)}`);
        const data = await res.json();
        const keys = data.keys || [];

        statKeyCount.textContent = keys.length;

        if (keys.length === 0) {
            keysContainer.innerHTML = `
                <div class="empty-state">
                    <p>No keys match '${escapeHtml(pattern)}'</p>
                    <span class="sub">Click "Sample Data" or use the terminal to create keys.</span>
                </div>
            `;
            return;
        }

        keysContainer.innerHTML = '';
        keys.forEach(item => {
            const card = document.createElement('div');
            card.className = 'key-card';

            const ttlText = item.ttl > 0 ? `TTL: ${item.ttl}s` : 'PERSISTENT';
            const badgeClass = item.type === 'list' ? 'badge-list' : 'badge-string';

            let valueDisplay = '';
            if (item.type === 'list' && Array.isArray(item.value)) {
                const chips = item.value.map((v, i) => `<span class="list-item-chip">[${i}] ${escapeHtml(String(v))}</span>`).join('');
                valueDisplay = `<div class="list-items">${chips}</div>`;
            } else {
                valueDisplay = `<span>${escapeHtml(String(item.value || ''))}</span>`;
            }

            card.innerHTML = `
                <div class="key-card-header">
                    <div class="key-name-wrap">
                        <span class="key-badge ${badgeClass}">${item.type}</span>
                        <span class="key-name">${escapeHtml(item.key)}</span>
                    </div>
                    <div class="key-actions">
                        <span class="ttl-badge">${ttlText}</span>
                        <button class="btn-del-key" data-key="${escapeHtml(item.key)}" title="Delete key">
                            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"></polyline><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path></svg>
                        </button>
                    </div>
                </div>
                <div class="key-value-box">
                    ${valueDisplay}
                </div>
            `;

            card.querySelector('.btn-del-key').addEventListener('click', async (e) => {
                const k = e.currentTarget.getAttribute('data-key');
                await fetch(`${API_BASE}/api/del?key=${encodeURIComponent(k)}`, { method: 'DELETE' });
                refreshKeyspace();
            });

            keysContainer.appendChild(card);
        });

    } catch (err) {
        console.error('Failed to load keys:', err);
    }
}

// Event Listeners
btnSendCmd.addEventListener('click', () => {
    executeCommand(terminalInput.value);
    terminalInput.value = '';
});

terminalInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
        executeCommand(terminalInput.value);
        terminalInput.value = '';
    } else if (e.key === 'ArrowUp') {
        if (historyIndex > 0) {
            historyIndex--;
            terminalInput.value = commandHistory[historyIndex] || '';
        }
    } else if (e.key === 'ArrowDown') {
        if (historyIndex < commandHistory.length - 1) {
            historyIndex++;
            terminalInput.value = commandHistory[historyIndex] || '';
        } else {
            historyIndex = commandHistory.length;
            terminalInput.value = '';
        }
    }
});

btnClearTerminal.addEventListener('click', () => {
    terminalOutput.innerHTML = '';
});

// Quick suggestion chips
document.querySelectorAll('.chip').forEach(chip => {
    chip.addEventListener('click', () => {
        const cmd = chip.getAttribute('data-cmd');
        terminalInput.value = cmd;
        executeCommand(cmd);
        terminalInput.value = '';
    });
});

// Search & Refresh
filterPattern.addEventListener('input', () => refreshKeyspace());
btnRefreshKeys.addEventListener('click', () => refreshKeyspace());

// Modal Handling
btnNewKeyModal.addEventListener('click', () => {
    newKeyModal.style.display = 'flex';
    inputKeyName.focus();
});

btnCloseModal.addEventListener('click', () => newKeyModal.style.display = 'none');
btnCancelModal.addEventListener('click', () => newKeyModal.style.display = 'none');

btnSubmitKey.addEventListener('click', async () => {
    const key = inputKeyName.value.trim();
    const val = inputKeyValue.value.trim();
    const ttl = parseInt(inputKeyTTL.value.trim(), 10) || null;

    if (!key) return;

    await fetch(`${API_BASE}/api/set`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ key, value: val, ttl })
    });

    newKeyModal.style.display = 'none';
    inputKeyName.value = '';
    inputKeyValue.value = '';
    inputKeyTTL.value = '';
    refreshKeyspace();
});

// Sample Data Button
btnSampleData.addEventListener('click', async () => {
    const samples = [
        'SET user:1001 "Alice Adams"',
        'SET user:1002 "Bob Baker"',
        'SET server:status "ONLINE"',
        'SET auth:session:active "token_89234" EX 30',
        'INCR metric:requests',
        'LPUSH queue:jobs "send_email" "generate_report" "sync_cache"',
    ];
    for (const cmd of samples) {
        await executeCommand(cmd);
    }
});

// Auto-refresh interval
setInterval(refreshKeyspace, 2000);

// Initial load
refreshKeyspace();
