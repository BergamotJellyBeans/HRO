'use strict';
(() => {
    const el = id => document.getElementById(id);
    let files = [], currentPage = null, currentOffset = 0, requestId = 0;
    let listLoading = false, listLoaded = false, windows = [], nextBefore = null;
    let rangeStart = '', rangeEnd = '';
    const time = value => new Intl.DateTimeFormat('en-GB', {
        timeZone: 'Asia/Tokyo', year: 'numeric', month: '2-digit', day: '2-digit',
        hour: '2-digit', minute: '2-digit', second: '2-digit', hourCycle: 'h23'
    }).format(new Date(value)) + '.' + String(value % 1000).padStart(3, '0');
    const color = value => {
        const t = Math.max(0, Math.min(1, (value + 20) / 50));
        if (t < .25) return [0, 0, 30 + 180 * t * 4];
        if (t < .5) return [0, 220 * (t - .25) * 4, 220];
        if (t < .75) return [255 * (t - .5) * 4, 220, 220 * (1 - (t - .5) * 4)];
        return [255, 220 + 35 * (t - .75) * 4, 255 * (t - .75) * 4];
    };
    async function getJSON(url) {
        const response = await fetch(url, {cache: 'no-store'});
        const data = await response.json();
        if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
        return data;
    }
    function clearCanvas(id) {
        const canvas = el(id), ctx = canvas.getContext('2d');
        ctx.fillStyle = '#080d18'; ctx.fillRect(0, 0, canvas.width, canvas.height);
        return ctx;
    }
    function selectRecord(index) {
        if (!currentPage || !currentPage.records.length) return;
        const record = currentPage.records[index];
        el('recordSlider').value = index;
        el('recordDetails').textContent = `Record ${record.index} · ${time(record.timestamp_ms)} JST · ` +
            `Sequence: ${record.sequence === null ? 'not stored (legacy)' : record.sequence}` +
            (record.valid ? '' : ` · Missing / invalid${record.error ? ': ' + record.error : ''}`);
        const ctx = clearCanvas('spectrumCanvas');
        ctx.fillStyle = '#ccc'; ctx.font = '14px monospace';
        ctx.fillText('FFT power (dB), without display correction', 64, 20);
        const left = 64, right = 1240, top = 35, bottom = 190;
        ctx.strokeStyle = '#344255'; ctx.lineWidth = 1;
        for (let db = -20; db <= 30; db += 10) {
            const y = bottom - (db + 20) / 50 * (bottom - top);
            ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(right, y); ctx.stroke();
            ctx.fillText(`${db}`, 22, y + 4);
        }
        const header = currentPage.header;
        ctx.fillText(`${header.FFT_MIN_HZ} Hz`, left, 220);
        ctx.fillText(`${header.FFT_CENTER_HZ} Hz`, 600, 220);
        ctx.fillText(`${header.FFT_MAX_HZ} Hz`, right - 60, 220);
        if (!record.valid) return;
        ctx.strokeStyle = '#66ff88'; ctx.beginPath();
        record.fft_db.forEach((db, i) => {
            const x = left + i / 600 * (right - left);
            const y = bottom - Math.max(0, Math.min(1, (db + 20) / 50)) * (bottom - top);
            if (i) ctx.lineTo(x, y); else ctx.moveTo(x, y);
        });
        ctx.stroke();
    }
    function drawPage() {
        const ctx = clearCanvas('hroCanvas');
        const {records, header} = currentPage;
        const left = 64, width = 1200, top = 32, height = 300;
        ctx.fillStyle = '#bbb'; ctx.font = '14px monospace';
        ctx.fillText(`${header.FFT_MAX_HZ} Hz`, 2, top + 10);
        ctx.fillText(`${header.FFT_CENTER_HZ} Hz`, 2, top + height / 2);
        ctx.fillText(`${header.FFT_MIN_HZ} Hz`, 2, top + height);
        if (!records.length) return;
        // Keep one pixel per record so no generated FFT is discarded by resampling.
        const image = ctx.createImageData(records.length, height);
        records.forEach((record, x) => {
            for (let y = 0; y < height; ++y) {
                const at = (y * records.length + x) * 4;
                const rgb = record.valid ? color(record.fft_db[Math.round((1-y/(height-1))*600)]) : [8,13,24];
                image.data[at] = rgb[0]; image.data[at+1] = rgb[1]; image.data[at+2] = rgb[2]; image.data[at+3] = 255;
            }
        });
        ctx.putImageData(image, left, top);
        ctx.fillStyle = '#ccc';
        ctx.fillText(`First: ${time(records[0].timestamp_ms)} JST`, left, 360);
        ctx.fillText(`Last: ${time(records[records.length-1].timestamp_ms)} JST`, left + width / 2, 360);
    }
    function controls(busy = false) {
        el('hroPrevious').disabled = busy || !currentPage || currentOffset === 0;
        el('hroNext').disabled = busy || !currentPage || !currentPage.has_more;
        el('recordSlider').disabled = busy || !currentPage || !currentPage.records.length;
    }
    async function loadPage(offset) {
        const selected = files.find(file => file.file === el('hroFile').value);
        const id = ++requestId;
        currentPage = null; controls(true); clearCanvas('hroCanvas'); clearCanvas('spectrumCanvas');
        el('recordDetails').textContent = '';
        if (!selected) { el('hroStatus').textContent = 'No HRO files.'; return; }
        if (selected.error) { el('hroStatus').textContent = selected.error; controls(); return; }
        el('hroStatus').textContent = 'Loading FFT records…';
        try {
            const data = await getJSON('/api/archive/hro/records?file=' + encodeURIComponent(selected.file) + '&offset=' + offset);
            if (id !== requestId) return;
            currentPage = data; currentOffset = offset;
            const valid = data.records.filter(record => record.valid).length;
            let breaks = 0;
            for (let i = 1; i < data.records.length; ++i) {
                const a = data.records[i-1], b = data.records[i];
                if (a.sequence !== null && b.sequence !== null && BigInt(b.sequence) !== BigInt(a.sequence) + 1n) ++breaks;
            }
            const count = data.records.length;
            el('hroStatus').textContent = `${selected.name} · ${data.layout === 'legacy' ? 'Legacy second slots' : 'Generation order'}\n` +
                `${data.recording ? 'Temporary file (recording or interrupted)' : 'Finalized'} · ` +
                `${data.record_count} records · Showing ${count ? offset + '–' + (offset+count-1) : 'none'}\n` +
                `Available in this page: ${valid}/${count} · Sequence discontinuities in this page: ${breaks}` +
                (data.trailing_bytes ? `\nIncomplete data: ${data.trailing_bytes} bytes ${data.layout === 'legacy' ? 'missing' : 'at tail (not displayed)'}` : '');
            el('recordSlider').max = Math.max(0, count-1); el('recordSlider').value = 0;
            drawPage(); if (count) selectRecord(0);
        } catch (error) {
            if (id === requestId) el('hroStatus').textContent = error.message + ' — try Refresh.';
        } finally { if (id === requestId) controls(); }
    }
    async function refreshFiles(older = false) {
        if (listLoading || (older && !nextBefore)) return;
        listLoading = true;
        const old = el('hroFile').value, offset = currentOffset;
        const id = ++requestId;
        let failed = false;
        el('hroOlder').textContent = 'Loading Archives…';
        el('hroOlder').setAttribute('data-state', 'loading');
        el('hroOlder').setAttribute('aria-busy', 'true');
        el('hroOlder').disabled = true; el('hroRefresh').disabled = true;
        controls(true); el('hroRange').textContent = 'Loading HRO files…';
        try {
            const cursors = older ? [nextBefore] : windows.length ? windows : [''];
            let merged = new Map((older ? files : []).map(file => [file.file, file]));
            const fetched = [];
            for (const cursor of cursors) {
                const data = await getJSON('/api/archive/hro/list' + (cursor ? '?before=' + encodeURIComponent(cursor) : ''));
                if (!Array.isArray(data.items)) throw new Error('Invalid HRO list response');
                fetched.push(data);
                for (const file of data.items) merged.set(file.file, file);
            }
            if (id !== requestId) return;
            files = [...merged.values()].sort((a,b) => a.file.localeCompare(b.file));
            windows = older ? [...windows, fetched[0].before] : fetched.map(data => data.before);
            const first = fetched[0], last = fetched[fetched.length-1];
            if (!older) rangeEnd = first.end_date;
            rangeStart = last.start_date; nextBefore = last.has_more ? last.next_before : null;
            listLoaded = true;
            el('hroRange').textContent = `${rangeStart} – ${rangeEnd} JST · ${files.length} HRO files` +
                (older && !last.items.length ? ' · No data in the added 30-day period.' : '') +
                (!last.has_more ? ' · No older archives.' : '');
            el('hroFile').replaceChildren();
            for (const file of files) {
                const option = document.createElement('option'); option.value = file.file;
                option.textContent = file.file + (file.error ? ' · Unavailable' : file.recording ? ' · Temporary' : '') +
                    (!file.error && file.trailing_bytes ? ' · Partial' : '');
                el('hroFile').appendChild(option);
            }
            const match = files.find(file => file.file === old) || files.find(file => file.file === old.replace(/\.tmp$/, ''));
            const selected = match || [...files].reverse().find(file => !file.error) || files[files.length-1];
            if (selected) el('hroFile').value = selected.file;
            if (!older || !match || !currentPage) await loadPage(match && match.record_count > offset ? offset : 0);
        } catch (error) {
            failed = true;
            if (id === requestId) el('hroRange').textContent = 'Failed to load HRO files. Please retry. ' + error.message;
        } finally {
            listLoading = false;
            const complete = !failed && !nextBefore;
            el('hroOlder').disabled = complete;
            el('hroOlder').textContent = failed ? 'Retry Loading Archives' : complete ? '✓ All Archives Loaded' : 'Load Older Archives (+30 Days)';
            el('hroOlder').setAttribute('data-state', complete ? 'complete' : 'ready');
            el('hroOlder').setAttribute('aria-busy', 'false');
            el('hroRefresh').disabled = false;
            controls();
        }
    }
    function mode(hro) {
        el('hroView').hidden = !hro; el('pngView').hidden = hro;
        el('hroMode').setAttribute('aria-pressed', String(hro));
        el('pngMode').setAttribute('aria-pressed', String(!hro));
        if (hro && !listLoaded) refreshFiles();
    }
    el('hroMode').onclick = () => mode(true); el('pngMode').onclick = () => mode(false);
    el('hroRefresh').onclick = () => refreshFiles();
    el('hroOlder').onclick = () => refreshFiles(nextBefore !== null);
    el('hroFile').onchange = () => loadPage(0);
    el('hroPrevious').onclick = () => loadPage(Math.max(0, currentOffset-1200));
    el('hroNext').onclick = () => loadPage(currentPage.next_offset);
    el('recordSlider').oninput = event => selectRecord(Number(event.target.value));
    el('hroCanvas').onclick = event => {
        if (!currentPage || !currentPage.records.length) return;
        const canvas = el('hroCanvas');
        const x = (event.clientX - canvas.getBoundingClientRect().left) * canvas.width / canvas.getBoundingClientRect().width;
        selectRecord(Math.max(0, Math.min(currentPage.records.length-1, Math.floor(x-64))));
    };
    controls();
})();
