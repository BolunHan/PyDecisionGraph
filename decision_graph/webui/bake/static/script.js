/* ==========================================================================
   Bake Graph — the page script.

   The graph arrives as a card model (see webui/bake/app.py) and is drawn as an
   SVG of cards on a grid. There is no simulation and no force layout: a node's
   place is a function of its depth and its order among its siblings, snapped to
   a fixed cell, so the same graph lands in the same place every time and an
   export is the picture on screen rather than a second drawing of it.
   ========================================================================== */

(function () {
    'use strict';

    // ---- Card geometry. The grid the layout snaps to. ----
    const CARD_W = 216;
    const CARD_H = 84;
    const BAND_H = 22;
    const CARD_PAD = 12;
    const CELL_X = CARD_W + 48;
    const CELL_Y = CARD_H + 46;
    const REPR_LINE_H = 16;
    const MAX_REPR_LINES = 2;
    const CHARS_PER_LINE = 30;
    const META_CHARS = 34;
    const BADGE_CHARS = 22;
    const CORNER = 9;

    const ZOOM_MIN = 0.15;
    const ZOOM_MAX = 3;
    const ZOOM_STEP = 1.25;

    const SVG_NS = 'http://www.w3.org/2000/svg';

    // ---- State ----
    const state = {
        data: null,
        byId: new Map(),
        refLinks: [],
        orientation: 'top-down',
        theme: 'dark',
        highlight: true,
        group: '*',
        activeIds: new Set(),
        hasWalk: false,
        walk: null,
        collapsed: new Set(),
        selected: null,
        failedId: null,
        zoom: null,
        cards: new Map(),
        edges: [],
    };

    // ---- DOM ----
    const el = {
        svg: document.getElementById('graph'),
        canvas: document.getElementById('canvas'),
        emptyNote: document.getElementById('empty-note'),
        titleName: document.getElementById('title-name'),
        statNodes: document.getElementById('stat-nodes'),
        statWalked: document.getElementById('stat-walked'),
        statMode: document.getElementById('stat-mode'),
        groups: document.getElementById('ctl-groups'),
        inspector: document.getElementById('inspector'),
        arms: document.getElementById('ins-arms'),
        pathBox: document.getElementById('ctl-path'),
        clipboardHint: document.getElementById('clipboard-hint'),
        outCode: document.getElementById('out-code'),
        outLength: document.getElementById('out-length'),
        outLeaf: document.getElementById('out-leaf'),
        evaluate: document.getElementById('btn-evaluate'),
        paste: document.getElementById('btn-paste-path'),
    };

    let viewport = null;
    let stylesheetText = '';

    // ======================================================================
    // Reading the payload
    // ======================================================================

    function readPayload() {
        const holder = document.getElementById('tree-data');
        if (!holder) return null;
        try {
            return JSON.parse(holder.textContent);
        } catch (error) {
            console.error('tree payload could not be parsed', error);
            return null;
        }
    }

    /**
     * Walk the card model once, collecting the records by id and the links that
     * are NOT child edges: a reference to an already-drawn node, and a
     * breakpoint's virtual parent. Both are drawn as extra edges afterwards,
     * because the layout must place each block exactly once.
     */
    function indexTree(data) {
        const byId = new Map();
        const refLinks = [];

        function walk(record) {
            if (!record || record.is_reference) return;
            byId.set(record.id, record);
            (record._children || []).forEach((arm) => {
                const child = arm.node;
                if (child && child.is_reference) {
                    refLinks.push({ source: record.id, target: child.id, kind: 'reference', condition: arm.condition });
                } else if (child) {
                    walk(child);
                }
            });
        }

        walk(data.root);
        (data.virtual_links || []).forEach((link) => {
            refLinks.push({ source: link.source, target: link.target, kind: 'virtual' });
        });

        return { byId, refLinks };
    }

    // ======================================================================
    // Layout
    // ======================================================================

    /** The tree the layout places: real children only, honouring collapses. */
    function buildLayoutNode(id) {
        const record = state.byId.get(id);
        const node = { id, record, children: [] };
        if (state.collapsed.has(id)) return node;
        (record._children || []).forEach((arm) => {
            if (arm.node && !arm.node.is_reference) {
                const child = buildLayoutNode(arm.node.id);
                child.condition = arm.condition;
                node.children.push(child);
            }
        });
        return node;
    }

    /**
     * Place every node on the grid.
     *
     * The tree layout gives each node a breadth (its order among its siblings)
     * and a depth; which screen axis each becomes is the whole of the
     * orientation switch.
     */
    function layout() {
        const rootData = buildLayoutNode(state.data.root.id);
        const hierarchy = d3.hierarchy(rootData, (d) => (d.children.length ? d.children : null));
        // Unit spacing: what the layout hands back is then an INDEX - a place in
        // the sibling order and a depth - and the cell sizes below are applied
        // per orientation. Letting the layout space the nodes instead would fix
        // one orientation's spacing and shear the other's.
        const tree = d3.tree().nodeSize([1, 1]);
        tree(hierarchy);

        const horizontal = state.orientation === 'top-down';
        const placed = [];
        hierarchy.each((d) => {
            const breadth = d.x;
            const depth = d.y;
            const cx = (horizontal ? breadth : depth) * CELL_X;
            const cy = (horizontal ? depth : breadth) * CELL_Y;
            d.card = { x: cx - CARD_W / 2, y: cy - CARD_H / 2 };
            placed.push(d);
        });
        return { hierarchy, placed, horizontal };
    }

    // ======================================================================
    // Drawing
    // ======================================================================

    function svgEl(name, attrs) {
        const node = document.createElementNS(SVG_NS, name);
        for (const key in attrs) node.setAttribute(key, attrs[key]);
        return node;
    }

    /** Break a display text into at most MAX_REPR_LINES lines of CHARS_PER_LINE. */
    function wrapText(text) {
        const source = String(text == null ? '' : text);
        if (source.length <= CHARS_PER_LINE) return [source];

        const lines = [];
        let rest = source;
        while (rest.length && lines.length < MAX_REPR_LINES) {
            if (rest.length <= CHARS_PER_LINE) {
                lines.push(rest);
                rest = '';
                break;
            }
            let cut = rest.lastIndexOf(' ', CHARS_PER_LINE);
            // No space to break at: cut mid-token rather than overflow the card.
            if (cut < CHARS_PER_LINE * 0.4) cut = CHARS_PER_LINE;
            lines.push(rest.slice(0, cut));
            rest = rest.slice(cut).replace(/^\s+/, '');
        }
        if (rest.length && lines.length) {
            lines[lines.length - 1] = lines[lines.length - 1].slice(0, CHARS_PER_LINE - 1) + '…';
        }
        return lines;
    }

    /** An SVG text has no clipping, so a long line is cut to fit the card. */
    function clamp(text, limit) {
        const source = String(text);
        return source.length <= limit ? source : source.slice(0, limit - 1) + '…';
    }

    function conditionClass(condition) {
        if (!condition) return 'is-value';
        if (condition.is_else) return 'is-else';
        if (condition.value === true) return 'is-true';
        if (condition.value === false) return 'is-false';
        return 'is-value';
    }

    function conditionText(condition) {
        if (!condition) return '';
        if (condition.is_else) return 'else';
        if (condition.is_none) return 'any';
        if (condition.value === true) return 'true';
        if (condition.value === false) return 'false';
        return String(condition.text || '');
    }

    /** The point at t=0.5 of a cubic bezier: where an edge's chip sits. */
    function bezierMid(p0, p1, p2, p3) {
        return {
            x: 0.125 * p0.x + 0.375 * p1.x + 0.375 * p2.x + 0.125 * p3.x,
            y: 0.125 * p0.y + 0.375 * p1.y + 0.375 * p2.y + 0.125 * p3.y,
        };
    }

    /** The two ends of an edge between two placed cards, and its curve. */
    function edgeGeometry(parent, child, horizontal) {
        const a = parent.card;
        const b = child.card;
        let p0, p1, p2, p3;

        if (horizontal) {
            // Depth runs down the screen: leave the bottom, arrive at the top.
            p0 = { x: a.x + CARD_W / 2, y: a.y + CARD_H };
            p3 = { x: b.x + CARD_W / 2, y: b.y };
        } else {
            // Depth runs across: leave the right side, arrive at the left.
            p0 = { x: a.x + CARD_W, y: a.y + CARD_H / 2 };
            p3 = { x: b.x, y: b.y + CARD_H / 2 };
        }

        const span = horizontal ? p3.y - p0.y : p3.x - p0.x;
        const lift = Math.max(18, Math.abs(span) * 0.42);
        p1 = horizontal ? { x: p0.x, y: p0.y + lift } : { x: p0.x + lift, y: p0.y };
        p2 = horizontal ? { x: p3.x, y: p3.y - lift } : { x: p3.x - lift, y: p3.y };

        return { p0, p1, p2, p3, mid: bezierMid(p0, p1, p2, p3) };
    }

    function pathOf(g) {
        return `M${g.p0.x},${g.p0.y} C${g.p1.x},${g.p1.y} ${g.p2.x},${g.p2.y} ${g.p3.x},${g.p3.y}`;
    }

    function drawCard(parentGroup, placed) {
        const rec = placed.data.record;
        const group = svgEl('g', {
            class: `card fam-${rec.family || 'OTHER'}`,
            transform: `translate(${placed.card.x},${placed.card.y})`,
        });
        group.dataset.id = rec.id;

        group.appendChild(svgEl('rect', {
            class: 'card-body', x: 0, y: 0, width: CARD_W, height: CARD_H,
            rx: CORNER, ry: CORNER,
        }));

        // The header band: a rounded rect with its lower corners squared off by
        // a second rect, which is cheaper than a clip path per card.
        group.appendChild(svgEl('path', {
            class: 'card-band',
            d: `M0,${BAND_H} V${CORNER} Q0,0 ${CORNER},0 H${CARD_W - CORNER} Q${CARD_W},0 ${CARD_W},${CORNER} V${BAND_H} Z`,
        }));

        const typeText = svgEl('text', { class: 'card-type', x: CARD_PAD, y: BAND_H / 2 + 1 });
        typeText.textContent = rec.type || '?';
        group.appendChild(typeText);

        const badges = [];
        if (rec.autogen) badges.push('autogen');
        if (rec.await_connection) badges.push('waiting');
        if (rec.hooks && rec.hooks.length) badges.push(rec.hooks.join(','));
        if (badges.length) {
            const badge = svgEl('text', { class: 'card-badge-text', x: CARD_W - CARD_PAD, y: BAND_H / 2 + 1 });
            badge.textContent = clamp(badges.join(' · '), BADGE_CHARS);
            group.appendChild(badge);
        }

        // Display text, wrapped. A leaf's text is the leaf's whole meaning, so
        // it gets the weight; a branch's is what the branch asks.
        const lines = wrapText(rec.repr);
        const textTop = BAND_H + 16 + (lines.length === 1 ? REPR_LINE_H / 2 : 0);
        lines.forEach((line, index) => {
            const text = svgEl('text', { class: 'card-repr', x: CARD_PAD, y: textTop + index * REPR_LINE_H });
            text.textContent = line;
            group.appendChild(text);
        });

        // The metadata line: what store the node belongs to, what it last
        // produced, and how much hangs below it.
        const meta = [];
        if (rec.labels && rec.labels.length) meta.push(rec.labels.join(','));
        if (rec.out !== null && rec.out !== undefined) meta.push(`out=${rec.out}`);
        if (rec.size > 1) meta.push(`${rec.size} nodes`);
        if (meta.length) {
            const metaText = svgEl('text', { class: 'card-meta', x: CARD_PAD, y: CARD_H - 14 });
            metaText.textContent = clamp(meta.join(' · '), META_CHARS);
            group.appendChild(metaText);
        }

        const armCount = (rec._children || []).filter((arm) => arm.node && !arm.node.is_reference).length;
        if (armCount) {
            const collapsed = state.collapsed.has(rec.id);
            const toggle = svgEl('g', { class: 'card-toggle' });
            toggle.appendChild(svgEl('rect', {
                class: 'card-toggle-bg', x: CARD_W - 30, y: CARD_H - 26, width: 22, height: 18, rx: 5, ry: 5,
            }));
            const label = svgEl('text', { class: 'card-toggle-text', x: CARD_W - 19, y: CARD_H - 16 });
            label.textContent = collapsed ? `+${armCount}` : '−';
            toggle.appendChild(label);
            toggle.addEventListener('click', (event) => {
                event.stopPropagation();
                if (state.collapsed.has(rec.id)) state.collapsed.delete(rec.id);
                else state.collapsed.add(rec.id);
                render();
            });
            group.appendChild(toggle);
        }

        group.addEventListener('click', (event) => {
            event.stopPropagation();
            select(rec.id);
        });

        parentGroup.appendChild(group);
        state.cards.set(rec.id, group);
    }

    function drawEdge(parentGroup, source, target, condition, kind) {
        const geometry = edgeGeometry(source, target, kind === 'horizontal');
        const path = svgEl('path', { class: 'edge', d: pathOf(geometry) });
        parentGroup.appendChild(path);
        const entry = { path, source: source.data.record.id, target: target.data.record.id };

        if (condition) {
            const chip = svgEl('g', { class: `edge-chip ${conditionClass(condition)}` });
            const text = conditionText(condition);
            const width = Math.max(30, text.length * 6.2 + 14);
            chip.appendChild(svgEl('rect', {
                class: 'edge-chip-bg', x: geometry.mid.x - width / 2, y: geometry.mid.y - 8,
                width, height: 16, rx: 8, ry: 8,
            }));
            const label = svgEl('text', { class: 'edge-chip-text', x: geometry.mid.x, y: geometry.mid.y + 1 });
            label.textContent = text;
            chip.appendChild(label);
            parentGroup.appendChild(chip);
            entry.chip = chip;
        }
        state.edges.push(entry);
        return entry;
    }

    function drawReference(parentGroup, source, target, kind) {
        const a = source.card;
        const b = target.card;
        const horizontal = kind === 'horizontal';
        // Source's far side to the target's near side, the same way a child edge
        // runs: the reference leaves the node it hangs off the way a branch does.
        const p0 = horizontal
            ? { x: a.x + CARD_W / 2, y: a.y + CARD_H }
            : { x: a.x + CARD_W, y: a.y + CARD_H / 2 };
        const p3 = horizontal
            ? { x: b.x + CARD_W / 2, y: b.y }
            : { x: b.x, y: b.y + CARD_H / 2 };
        const curve = horizontal
            ? `M${p0.x},${p0.y} C${p0.x},${(p0.y + p3.y) / 2} ${p3.x},${(p0.y + p3.y) / 2} ${p3.x},${p3.y}`
            : `M${p0.x},${p0.y} C${(p0.x + p3.x) / 2},${p0.y} ${(p0.x + p3.x) / 2},${p3.y} ${p3.x},${p3.y}`;
        const path = svgEl('path', { class: 'vlink', d: curve });
        parentGroup.appendChild(path);
        state.edges.push({ path, source: source.data.record.id, target: target.data.record.id, virtual: true });
    }

    // ======================================================================
    // Render
    // ======================================================================

    function render() {
        state.cards = new Map();
        state.edges = [];

        const { hierarchy, placed, horizontal } = layout();
        const edgesGroup = svgEl('g', { id: 'edges' });
        const cardsGroup = svgEl('g', { id: 'cards' });

        const byLayoutId = new Map();
        placed.forEach((d) => byLayoutId.set(d.data.record.id, d));

        hierarchy.each((d) => {
            (d.children || []).forEach((child) => {
                // `child.data` is the layout node; `child` is d3's wrapper.
                drawEdge(edgesGroup, d, child, child.data.condition, horizontal ? 'horizontal' : 'vertical');
            });
        });

        state.refLinks.forEach((link) => {
            const source = byLayoutId.get(link.source);
            const target = byLayoutId.get(link.target);
            if (source && target) drawReference(edgesGroup, source, target, horizontal ? 'horizontal' : 'vertical');
        });

        placed.forEach((d) => drawCard(cardsGroup, d));

        viewport.textContent = '';
        viewport.appendChild(edgesGroup);
        viewport.appendChild(cardsGroup);

        paint();
    }

    /** Apply the filters and the selection to what is already drawn. */
    function paint() {
        const walked = state.highlight && state.hasWalk;
        state.cards.forEach((group, id) => {
            const rec = state.byId.get(id);
            const dimmed =
                (state.group !== '*' && !(rec.labels || []).includes(state.group)) ||
                (walked && !state.activeIds.has(id));
            group.classList.toggle('is-active', walked && state.activeIds.has(id));
            group.classList.toggle('is-dim', dimmed);
            group.classList.toggle('is-selected', state.selected === id);
            group.classList.toggle('is-failed', state.failedId === id);
        });

        state.edges.forEach((entry) => {
            const onPath = walked && entry.source && entry.target &&
                state.activeIds.has(entry.source) && state.activeIds.has(entry.target);
            entry.path.classList.toggle('is-active', onPath);
            entry.path.classList.toggle('is-dim', walked && !onPath);
        });
    }

    // ======================================================================
    // Zoom and pan
    // ======================================================================

    function fit() {
        if (!state.cards.size || !state.zoom) return;
        const box = viewport.getBBox();
        const stage = el.svg.getBoundingClientRect();
        const pad = 48;
        const scale = Math.min(
            (stage.width - pad * 2) / Math.max(box.width, 1),
            (stage.height - pad * 2) / Math.max(box.height, 1),
            1.4,
        );
        const tx = stage.width / 2 - (box.x + box.width / 2) * scale;
        const ty = stage.height / 2 - (box.y + box.height / 2) * scale;
        // A selection, not the element: the zoom behaviour works on d3's
        // wrapper, and handing it a bare DOM node fails inside d3.
        d3.select(el.svg).call(state.zoom.transform, d3.zoomIdentity.translate(tx, ty).scale(scale));
    }

    function zoomBy(factor) {
        d3.select(el.svg).call(state.zoom.scaleBy, factor);
    }

    function setupZoom() {
        state.zoom = d3.zoom()
            .scaleExtent([ZOOM_MIN, ZOOM_MAX])
            .on('zoom', (event) => {
                viewport.setAttribute('transform', event.transform.toString());
            });

        d3.select(el.svg).call(state.zoom);

        // A drag on a card is a drag on the canvas: panning is the gesture
        // people reach for, and nothing here is drag-to-move.
        d3.select(el.svg).on('dblclick.zoom', null);
    }

    // ======================================================================
    // The panel on the right
    // ======================================================================

    const inspectorFields = {
        'ins-type': (rec) => rec.type,
        'ins-repr': (rec) => rec.repr,
        'ins-id': (rec) => rec.id,
        'ins-address': (rec) => `0x${Number(rec.address).toString(16)}`,
        'ins-family': (rec) => rec.family,
        'ins-labels': (rec) => (rec.labels && rec.labels.length ? rec.labels.join(', ') : '—'),
        'ins-out': (rec) => (rec.out === null || rec.out === undefined ? '—' : rec.out),
        'ins-hooks': (rec) => (rec.hooks && rec.hooks.length ? rec.hooks.join(', ') : '—'),
        'ins-size': (rec) => `${rec.size} node(s)`,
        'ins-autogen': (rec) => (rec.autogen ? 'yes' : 'no'),
    };

    function select(id) {
        const rec = state.byId.get(id);
        if (!rec) return;
        state.selected = id;

        for (const field in inspectorFields) {
            const target = document.getElementById(field);
            const value = inspectorFields[field](rec);
            target.textContent = value === null || value === undefined ? '—' : String(value);
        }

        el.arms.textContent = '';
        (rec._children || []).forEach((arm) => {
            const item = document.createElement('li');
            const key = document.createElement('span');
            key.className = `arm-key ${conditionClass(arm.condition)}`;
            key.textContent = conditionText(arm.condition);
            const name = document.createElement('span');
            name.className = 'arm-name';
            name.textContent = arm.node && arm.node.is_reference
                ? `${arm.node.id} (drawn elsewhere)`
                : `${arm.node ? arm.node.type : '?'} — ${arm.node ? arm.node.repr : ''}`;
            item.appendChild(key);
            item.appendChild(name);
            el.arms.appendChild(item);
        });

        el.inspector.hidden = false;
        paint();
    }

    // ======================================================================
    // Walk state
    // ======================================================================

    function applyWalk(payload) {
        if (!payload) return;
        state.walk = payload;
        state.activeIds = new Set(payload.active_ids || []);
        state.hasWalk = state.activeIds.size > 0;
        state.failedId = payload.failed || null;

        el.outCode.textContent = payload.code || '—';
        el.outLength.textContent = payload.length === undefined ? state.activeIds.size : String(payload.length);
        const leaf = payload.leaf ? state.byId.get(payload.leaf) : null;
        el.outLeaf.textContent = leaf ? `${leaf.type} — ${leaf.repr}` : '—';
        el.statWalked.textContent = String(state.activeIds.size);
        paint();
    }

    async function evaluate() {
        if (window.bake_viewer.offline) {
            setHint('An exported page cannot walk the graph — it has no process behind it.', true);
            return;
        }
        el.evaluate.disabled = true;
        try {
            const response = await fetch('/api/evaluate', { method: 'POST' });
            const payload = await response.json();
            applyWalk(payload);
            if (payload.error) setHint(`Walk failed: ${payload.error}`, true);
            else setHint('');
        } catch (error) {
            setHint(`Walk failed: ${error.message}`, true);
        } finally {
            el.evaluate.disabled = false;
        }
    }

    function watchStream() {
        if (window.bake_viewer.offline || !window.bake_viewer.with_watch) return;
        const source = new EventSource('/watch');
        source.onmessage = (event) => {
            try {
                const diff = JSON.parse(event.data);
                (diff.removed || []).forEach((id) => state.activeIds.delete(id));
                (diff.added || []).forEach((id) => state.activeIds.add(id));
                state.hasWalk = state.activeIds.size > 0;
                el.statWalked.textContent = String(state.activeIds.size);
                paint();
            } catch (error) {
                console.error('watch payload could not be parsed', error);
            }
        };
    }

    // ======================================================================
    // Clipboard
    // ======================================================================

    function setHint(text, isError) {
        el.clipboardHint.textContent = text || '';
        el.clipboardHint.classList.toggle('is-error', Boolean(isError));
        el.clipboardHint.classList.toggle('is-ok', Boolean(text) && !isError);
    }

    async function copyPath() {
        const ids = Array.from(state.activeIds);
        if (!ids.length) {
            setHint('Nothing has been walked yet — press Evaluate first.', true);
            return;
        }
        const payload = JSON.stringify(ids);
        el.pathBox.value = payload;
        try {
            await navigator.clipboard.writeText(payload);
            setHint(`Copied ${ids.length} node id(s).`);
        } catch (error) {
            setHint('Clipboard is not available — the ids are in the box, copy them from there.', true);
        }
    }

    async function pastePath() {
        try {
            el.pathBox.value = await navigator.clipboard.readText();
            setHint('Read the clipboard. Press Load to highlight it.');
        } catch (error) {
            setHint('Clipboard is not available — paste into the box by hand.', true);
        }
    }

    function loadPath() {
        let ids;
        try {
            ids = JSON.parse(el.pathBox.value);
        } catch (error) {
            setHint('That is not JSON. Expected a list of node ids.', true);
            return;
        }
        if (!Array.isArray(ids)) {
            setHint('Expected a list of node ids.', true);
            return;
        }
        const known = ids.filter((id) => state.byId.has(id));
        state.activeIds = new Set(known);
        state.hasWalk = known.length > 0;
        state.failedId = null;
        el.statWalked.textContent = String(known.length);
        el.outCode.textContent = 'loaded';
        el.outLength.textContent = String(known.length);
        el.outLeaf.textContent = '—';
        paint();
        setHint(known.length === ids.length
            ? `Loaded ${known.length} node id(s).`
            : `Loaded ${known.length} of ${ids.length} id(s) — the rest are not in this graph.`,
            known.length !== ids.length);
    }

    function clearPath() {
        state.activeIds = new Set();
        state.hasWalk = false;
        state.failedId = null;
        el.pathBox.value = '';
        el.statWalked.textContent = '0';
        el.outCode.textContent = '—';
        el.outLength.textContent = '—';
        el.outLeaf.textContent = '—';
        paint();
        setHint('');
    }

    // ======================================================================
    // Export
    // ======================================================================

    const THEME_VARS = [
        '--bg', '--bg-grid', '--panel', '--panel-2', '--item', '--line',
        '--text', '--text-dim', '--text-faint', '--accent', '--accent-soft',
        '--ok', '--warn', '--bad',
        '--card-bg', '--card-line', '--card-repr', '--card-meta', '--card-active-ring',
        '--fam-input', '--fam-op', '--fam-action', '--fam-special', '--fam-other', '--fam-on',
        '--font-ui', '--font-mono',
    ];

    function exportableSvg() {
        if (!state.cards.size) {
            alert('Nothing to export.');
            return null;
        }
        const box = viewport.getBBox();
        const pad = 32;
        const clone = el.svg.cloneNode(true);

        clone.setAttribute('xmlns', SVG_NS);
        clone.setAttribute('viewBox', `${box.x - pad} ${box.y - pad} ${box.width + pad * 2} ${box.height + pad * 2}`);
        clone.setAttribute('width', Math.ceil(box.width + pad * 2));
        clone.setAttribute('height', Math.ceil(box.height + pad * 2));

        // The clone carries no stylesheet, so the theme values it is drawn with
        // are written into it - the same names the stylesheet reads.
        const computed = getComputedStyle(document.documentElement);
        let declarations = ':root{';
        THEME_VARS.forEach((name) => {
            const value = computed.getPropertyValue(name).trim();
            if (value) declarations += `${name}:${value};`;
        });
        declarations += `}\n#graph{background:${computed.getPropertyValue('--bg-grid').trim()};}`;

        if (!stylesheetText) stylesheetText = readStylesheet();
        const style = document.createElementNS(SVG_NS, 'style');
        style.textContent = declarations + stylesheetText;
        clone.insertBefore(style, clone.firstChild);

        // The viewport transform is dropped: the exported viewBox is the whole
        // graph, so whatever the screen was panned to is not the picture.
        const view = clone.querySelector('g');
        if (view) view.removeAttribute('transform');

        return { svg: clone, width: box.width + pad * 2, height: box.height + pad * 2 };
    }

    function downloadBlob(blob, filename) {
        const url = URL.createObjectURL(blob);
        const anchor = document.createElement('a');
        anchor.href = url;
        anchor.download = filename;
        document.body.appendChild(anchor);
        anchor.click();
        anchor.remove();
        setTimeout(() => URL.revokeObjectURL(url), 100);
    }

    function exportSVG() {
        const drawn = exportableSvg();
        if (!drawn) return;
        const text = new XMLSerializer().serializeToString(drawn.svg);
        downloadBlob(new Blob([text], { type: 'image/svg+xml;charset=utf-8' }), 'bake-graph.svg');
    }

    function exportPNG() {
        const drawn = exportableSvg();
        if (!drawn) return;
        const scale = 2;
        const text = new XMLSerializer().serializeToString(drawn.svg);
        const image = new Image();
        const url = URL.createObjectURL(new Blob([text], { type: 'image/svg+xml;charset=utf-8' }));

        image.onload = () => {
            const canvas = document.createElement('canvas');
            canvas.width = Math.ceil(drawn.width * scale);
            canvas.height = Math.ceil(drawn.height * scale);
            const context = canvas.getContext('2d');
            context.scale(scale, scale);
            context.drawImage(image, 0, 0);
            URL.revokeObjectURL(url);
            canvas.toBlob((blob) => downloadBlob(blob, 'bake-graph.png'), 'image/png');
        };
        image.onerror = () => {
            URL.revokeObjectURL(url);
            alert('The PNG export could not rasterize the drawing. The SVG export still works.');
        };
        image.src = url;
    }

    // ======================================================================
    // Controls
    // ======================================================================

    function setOrientation(orientation) {
        state.orientation = orientation;
        document.documentElement.dataset.orientation = orientation;
        document.querySelectorAll('#ctl-orientation button').forEach((button) => {
            button.classList.toggle('active', button.dataset.orientation === orientation);
        });
        render();
        fit();
    }

    function setTheme(theme) {
        state.theme = theme;
        document.documentElement.dataset.theme = theme;
        document.querySelectorAll('#ctl-theme button').forEach((button) => {
            button.classList.toggle('active', button.dataset.theme === theme);
        });
    }

    /** Paint the segmented controls from the state, without re-drawing. */
    function syncControls() {
        document.querySelectorAll('#ctl-orientation button').forEach((button) => {
            button.classList.toggle('active', button.dataset.orientation === state.orientation);
        });
        document.querySelectorAll('#ctl-theme button').forEach((button) => {
            button.classList.toggle('active', button.dataset.theme === state.theme);
        });
    }

    function setGroup(group) {
        state.group = group;
        document.querySelectorAll('#ctl-groups .chip').forEach((chip) => {
            chip.classList.toggle('active', chip.dataset.group === group);
        });
        paint();
    }

    function buildGroupChips() {
        const groups = new Set();
        state.byId.forEach((rec) => (rec.labels || []).forEach((label) => groups.add(label)));
        const sorted = Array.from(groups).sort();

        el.groups.textContent = '';
        const make = (name, text) => {
            const chip = document.createElement('button');
            chip.type = 'button';
            chip.className = `chip${name === '*' ? ' active' : ''}`;
            chip.dataset.group = name;
            chip.textContent = text;
            chip.addEventListener('click', () => setGroup(name));
            el.groups.appendChild(chip);
        };
        make('*', 'All');
        sorted.forEach((name) => make(name, name));
        return sorted.length;
    }

    function wire() {
        document.querySelectorAll('#ctl-orientation button').forEach((button) => {
            button.addEventListener('click', () => setOrientation(button.dataset.orientation));
        });
        document.querySelectorAll('#ctl-theme button').forEach((button) => {
            button.addEventListener('click', () => setTheme(button.dataset.theme));
        });
        document.getElementById('ctl-highlight').addEventListener('change', (event) => {
            state.highlight = event.target.checked;
            paint();
        });
        document.getElementById('btn-zoom-in').addEventListener('click', () => zoomBy(ZOOM_STEP));
        document.getElementById('btn-zoom-out').addEventListener('click', () => zoomBy(1 / ZOOM_STEP));
        document.getElementById('btn-zoom-reset').addEventListener('click', fit);
        document.getElementById('btn-copy-path').addEventListener('click', copyPath);
        document.getElementById('btn-paste-path').addEventListener('click', pastePath);
        document.getElementById('btn-load-path').addEventListener('click', loadPath);
        document.getElementById('btn-clear-path').addEventListener('click', clearPath);
        document.getElementById('btn-export-svg').addEventListener('click', exportSVG);
        document.getElementById('btn-export-png').addEventListener('click', exportPNG);
        el.evaluate.addEventListener('click', evaluate);
        document.getElementById('ins-close').addEventListener('click', () => {
            el.inspector.hidden = true;
            state.selected = null;
            paint();
        });
        document.addEventListener('keydown', (event) => {
            if (event.key === 'Escape') {
                el.inspector.hidden = true;
                state.selected = null;
                paint();
            }
            // Ctrl/Cmd + 0 fits the graph, the way every canvas app does.
            if ((event.ctrlKey || event.metaKey) && event.key === '0') {
                event.preventDefault();
                fit();
            }
        });
        // A click on the empty canvas clears the selection.
        el.svg.addEventListener('click', () => {
            if (state.selected === null) return;
            state.selected = null;
            el.inspector.hidden = true;
            paint();
        });
    }

    // ======================================================================
    // Boot
    // ======================================================================

    function boot() {
        const data = readPayload();
        if (!data || !data.root || !data.root.id) {
            showEmpty('Nothing to draw — the viewer is holding no graph.');
            return;
        }
        state.data = data;

        const index = indexTree(data);
        state.byId = index.byId;
        state.refLinks = index.refLinks;

        el.titleName.textContent = data.root_name || data.root.repr || data.root.id;
        el.statNodes.textContent = String(data.n_nodes || state.byId.size);
        // What is counted is what is DRAWN: a node reached only as an operand
        // is not a card, and the graph holds more of those than it shows.
        el.statNodes.parentElement.title =
            'Cards drawn: a node reached only as an operand of another is not one of them.';
        showScaffolding(data.scaffolding);

        buildGroupChips();
        wire();

        viewport = svgEl('g', { id: 'bake-viewport' });
        el.svg.appendChild(viewport);
        setupZoom();
        syncControls();

        indexWalkFromPayload();
        render();
        fit();
        watchStream();
    }

    /**
     * What the graph still carries of the BUILD.
     *
     * A breakpoint a split build stopped at and a placeholder a branch reserved
     * and never filled are both gone once the bake has run, so counting them is
     * what tells the two states of a graph apart - without having to have been
     * handed the report.
     */
    function showScaffolding(scaffolding) {
        const counts = scaffolding || { breakpoints: 0, placeholders: 0 };
        const parts = [];
        if (counts.breakpoints) parts.push(`${counts.breakpoints} breakpoint`);
        if (counts.placeholders) parts.push(`${counts.placeholders} placeholder`);
        if (parts.length) {
            el.statMode.textContent = `unbaked · ${parts.join(' · ')}`;
            el.statMode.title = 'The bake takes this down: what the scaffolding carried takes its place.';
        } else {
            el.statMode.textContent = 'no scaffolding';
            el.statMode.title = 'Neither a breakpoint nor a stand-in is left in this graph.';
        }
    }

    /** The payload may already carry a walk (to_html with eval, or show()). */
    function indexWalkFromPayload() {
        if (!Array.isArray(state.data.active_ids) || !state.data.active_ids.length) return;
        state.activeIds = new Set(state.data.active_ids.filter((id) => state.byId.has(id)));
        state.hasWalk = state.activeIds.size > 0;
        state.walk = state.data.walk || null;
        if (state.walk) {
            state.failedId = state.walk.failed || null;
            el.outCode.textContent = state.walk.code || '—';
            el.outLength.textContent = String(state.walk.length === undefined ? state.activeIds.size : state.walk.length);
            const leaf = state.walk.leaf ? state.byId.get(state.walk.leaf) : null;
            el.outLeaf.textContent = leaf ? `${leaf.type} — ${leaf.repr}` : '—';
        }
        el.statWalked.textContent = String(state.activeIds.size);
    }

    /**
     * The stylesheet text, read once so the export can carry it.
     *
     * Every readable sheet is collected rather than the one this page links:
     * an exported file has no <link> to follow, and the offline page has no
     * link at all - its stylesheet is already inline.
     */
    function readStylesheet() {
        let text = '';
        for (const sheet of document.styleSheets) {
            try {
                if (!sheet.cssRules) continue;
                for (const rule of sheet.cssRules) text += rule.cssText + '\n';
            } catch (error) {
                /* a cross-origin sheet: it cannot be read, and cannot be exported */
            }
        }
        return text;
    }

    /**
     * Say what went wrong, in the middle of the stage.
     *
     * A page that draws nothing and says nothing is the worst of both: the
     * reader cannot tell an empty graph from a broken drawing.
     */
    function showEmpty(message) {
        el.emptyNote.textContent = message;
        el.emptyNote.hidden = false;
        el.emptyNote.classList.add('is-visible');
    }

    window.addEventListener('error', (event) => {
        showEmpty(`The drawing failed: ${event.message}`);
    });

    function start() {
        try {
            boot();
        } catch (error) {
            showEmpty(`The drawing failed: ${error && error.message ? error.message : error}`);
            console.error(error);
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', start);
    } else {
        start();
    }
})();
