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
    //
    // The card's size belongs to the reader rather than to the layout: a node's
    // display text is as long as it is, and a card that cannot grow has to cut
    // it off. So the WIDTH is what the text wraps to and the HEIGHT is how much
    // of it fits - a card scaled by a transform would only make the same
    // overflow bigger.
    const BAND_H = 22;
    const CARD_PAD = 12;
    const REPR_LINE_H = 16;
    const META_ROW_H = 24;   // The strip along the bottom the metadata sits in.
    const CHAR_W = 6.55;     // One character of the display text, in the mono font.
    const CORNER = 9;
    const CHIP_CHARS = 30;   // A condition chip is on the edge, not on a card.

    const ZOOM_MIN = 0.15;
    const ZOOM_MAX = 3;
    const ZOOM_STEP = 1.25;

    const SVG_NS = 'http://www.w3.org/2000/svg';

    // ---- Presentation: what the sliders drive. ----
    const PRESENTATION_DEFAULTS = {
        levelGap: 84,      // Between one level and the next: the edge's length.
        siblingGap: 48,    // Between two cards of the same level.
        cardWidth: 232,    // What a display text wraps to.
        cardHeight: 100,   // How much of it fits.
        edgeWidth: 1.6,    // Stroke width of a link.
        animSpeed: 100,    // Percent: 100 is the base duration, 1 is a hundredth.
    };
    const presentation = { ...PRESENTATION_DEFAULTS };

    /** The size a card ends up on screen, and the cell it takes with it. */
    const cardW = () => presentation.cardWidth;
    const cardH = () => presentation.cardHeight;
    const cellX = () => cardW() + presentation.siblingGap;
    const cellY = () => cardH() + presentation.levelGap;

    /** How much display text a card of the current size holds. */
    const charsPerLine = () => Math.max(6, Math.floor((cardW() - CARD_PAD * 2) / CHAR_W));
    const reprLines = () => Math.max(1, Math.floor((cardH() - BAND_H - META_ROW_H) / REPR_LINE_H));

    // How long a layout animation runs at speed x1. The slider scales it: x0.01
    // is a hundred times slower, which is a debugging speed rather than a
    // viewing one.
    const ANIM_BASE_MS = 300;

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
        positions: new Map(),
        edgesGroup: null,
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
        operands: document.getElementById('ins-operands'),
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
            const cx = (horizontal ? breadth : depth) * cellX();
            const cy = (horizontal ? depth : breadth) * cellY();
            d.card = { x: cx - cardW() / 2, y: cy - cardH() / 2 };
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

    /**
     * Break a display text to fit the card: `perLine` characters, `maxLines` lines.
     *
     * Both come from the card's current size rather than from a constant, so
     * widening a card lets a long expression onto one line and heightening one
     * lets it onto the next line instead of being dropped.
     */
    function wrapText(text, perLine, maxLines) {
        const source = String(text == null ? '' : text);
        if (source.length <= perLine) return [source];

        const lines = [];
        let rest = source;
        while (rest.length && lines.length < maxLines) {
            if (rest.length <= perLine) {
                lines.push(rest);
                rest = '';
                break;
            }
            let cut = rest.lastIndexOf(' ', perLine);
            // No space to break at: cut mid-token rather than overflow the card.
            if (cut < perLine * 0.4) cut = perLine;
            lines.push(rest.slice(0, cut));
            rest = rest.slice(cut).replace(/^\s+/, '');
        }
        if (rest.length && lines.length) {
            lines[lines.length - 1] = lines[lines.length - 1].slice(0, perLine - 1) + '…';
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
            p0 = { x: a.x + cardW() / 2, y: a.y + cardH() };
            p3 = { x: b.x + cardW() / 2, y: b.y };
        } else {
            // Depth runs across: leave the right side, arrive at the left.
            p0 = { x: a.x + cardW(), y: a.y + cardH() / 2 };
            p3 = { x: b.x, y: b.y + cardH() / 2 };
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
        const W = cardW();
        const H = cardH();
        const group = svgEl('g', {
            // The TYPE as well as the family: a family says where a node sits,
            // the type says what it does, and for the action family that is the
            // whole of what a reader wants off the card.
            class: `card fam-${rec.family || 'OTHER'} type-${rec.type || 'UNKNOWN'}`,
            transform: `translate(${placed.card.x},${placed.card.y})`,
        });
        group.dataset.id = rec.id;

        group.appendChild(svgEl('rect', {
            class: 'card-body', x: 0, y: 0, width: W, height: H,
            rx: CORNER, ry: CORNER,
        }));

        // The header band: a rounded rect with its lower corners squared off by
        // the path itself, which is cheaper than a clip path per card.
        group.appendChild(svgEl('path', {
            class: 'card-band',
            d: `M0,${BAND_H} V${CORNER} Q0,0 ${CORNER},0 H${W - CORNER} Q${W},0 ${W},${CORNER} V${BAND_H} Z`,
        }));

        const typeText = svgEl('text', { class: 'card-type', x: CARD_PAD, y: BAND_H / 2 + 1 });
        typeText.textContent = rec.type || '?';
        group.appendChild(typeText);

        const badges = [];
        if (rec.autogen) badges.push('autogen');
        if (rec.await_connection) badges.push('waiting');
        if (rec.operands && rec.operands.length) badges.push(`${rec.operands.length} ops`);
        if (rec.hooks && rec.hooks.length) badges.push(rec.hooks.join(','));
        if (badges.length) {
            const badge = svgEl('text', { class: 'card-badge-text', x: W - CARD_PAD, y: BAND_H / 2 + 1 });
            // The badges share the band with the type, so they get what is left
            // of it rather than a width of their own.
            const room = Math.max(6, Math.floor((W - CARD_PAD * 3 - typeText.textContent.length * 7) / CHAR_W));
            badge.textContent = clamp(badges.join(' · '), room);
            group.appendChild(badge);
        }

        // Display text, wrapped to the card's width and centred in the space
        // the band and the metadata strip leave.
        const lines = wrapText(rec.repr, charsPerLine(), reprLines());
        const top = BAND_H;
        const bottom = H - META_ROW_H;
        const block = lines.length * REPR_LINE_H;
        const first = top + Math.max(0, (bottom - top - block) / 2) + REPR_LINE_H * 0.7;
        lines.forEach((line, index) => {
            const text = svgEl('text', { class: 'card-repr', x: CARD_PAD, y: first + index * REPR_LINE_H });
            text.textContent = line;
            group.appendChild(text);
        });

        // The metadata line: what group the node belongs to, what it holds
        // itself, what it last produced, and how much hangs below it.
        const meta = [];
        if (rec.labels && rec.labels.length) meta.push(rec.labels.join(','));
        // What the node holds ITSELF comes before what it produced: a literal's
        // value and a read's entry are the node's meaning, the output is a
        // consequence of it.
        if (rec.self_value !== null && rec.self_value !== undefined) meta.push(`=${rec.self_value}`);
        if (rec.out !== null && rec.out !== undefined) meta.push(`out=${rec.out}`);
        if (rec.size > 1) meta.push(`${rec.size} nodes`);
        if (meta.length) {
            const metaText = svgEl('text', { class: 'card-meta', x: CARD_PAD, y: H - 12 });
            metaText.textContent = clamp(meta.join(' · '), charsPerLine() + 4);
            group.appendChild(metaText);
        }

        const armCount = (rec._children || []).filter((arm) => arm.node && !arm.node.is_reference).length;
        if (armCount) {
            const collapsed = state.collapsed.has(rec.id);
            const toggle = svgEl('g', { class: 'card-toggle' });
            toggle.appendChild(svgEl('rect', {
                class: 'card-toggle-bg', x: W - 30, y: H - 26, width: 22, height: 18, rx: 5, ry: 5,
            }));
            const label = svgEl('text', { class: 'card-toggle-text', x: W - 19, y: H - 16 });
            label.textContent = collapsed ? `+${armCount}` : '−';
            toggle.appendChild(label);
            toggle.addEventListener('click', (event) => {
                event.stopPropagation();
                if (state.collapsed.has(rec.id)) state.collapsed.delete(rec.id);
                else state.collapsed.add(rec.id);
                render({ animate: true });
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
            ? { x: a.x + cardW() / 2, y: a.y + cardH() }
            : { x: a.x + cardW(), y: a.y + cardH() / 2 };
        const p3 = horizontal
            ? { x: b.x + cardW() / 2, y: b.y }
            : { x: b.x, y: b.y + cardH() / 2 };
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

    /** The duration of the next layout animation, from the speed slider. */
    function animationMs() {
        return ANIM_BASE_MS * (100 / Math.max(1, presentation.animSpeed));
    }

    const easeOut = (t) => 1 - Math.pow(1 - t, 3);

    // Superseding token: a second change while one is running calls off the
    // first, which would otherwise go on writing to nodes nobody can see.
    let animationRun = 0;

    /**
     * Drive a value from 0 to 1 over `ms`, on the frame clock.
     *
     * The animation is written here rather than handed to CSS on purpose. A
     * transition needs the browser to have committed a "before" value, and the
     * nodes it would run on were made a moment earlier in the same turn - so
     * whether the browser sees two states or one coalesced write is its choice,
     * not ours, and the answer differs between them. Setting the value per
     * frame is the same work and none of the guessing.
     */
    function runAnimation(frame, ms, done) {
        const run = ++animationRun;
        const started = performance.now();

        const step = (now) => {
            if (run !== animationRun) return;   // called off: leave the nodes be
            const travelled = Math.min(1, (now - started) / ms);
            frame(easeOut(travelled));
            if (travelled < 1) requestAnimationFrame(step);
            else done();
        };
        requestAnimationFrame(step);
    }

    /**
     * Carry the cards from where they were to where they now are.
     *
     * The layout is recomputed from scratch on every change, so a card that
     * moved would jump straight there. Three things have to be put right:
     *
     *   - a card that MOVED is set back where it was and travels;
     *   - a card that ARRIVED fades in;
     *   - a card - or a link, which is redrawn wholesale and so is never "the
     *     same link" twice - that has LEFT keeps a copy on screen to fade away,
     *     because it is out of the drawing by then and nothing can animate it.
     */
    function animateLayout(previous, positions, previousCards, previousEdges) {
        const ms = animationMs();

        const ghost = svgEl('g', { class: 'ghost-layer' });
        if (previousEdges) ghost.appendChild(previousEdges.cloneNode(true));
        previousCards.forEach((group, id) => {
            if (positions.has(id)) return;   // it survives, and travels instead
            ghost.appendChild(group.cloneNode(true));
        });
        const leaving = ghost.childNodes.length > 0;
        if (leaving) {
            ghost.style.opacity = '1';
            viewport.appendChild(ghost);
        }

        const moving = [];
        const arriving = [];
        state.cards.forEach((group, id) => {
            const was = previous.get(id);
            const now = positions.get(id);
            if (!now) return;
            if (!was) {
                group.style.opacity = '0';
                arriving.push(group);
                return;
            }
            if (was.x === now.x && was.y === now.y) return;
            group.setAttribute('transform', `translate(${was.x},${was.y})`);
            moving.push({ group, from: was, to: now });
        });

        // Taken now rather than read from the state each frame: a redraw while
        // this runs replaces the list, and the animation is about the nodes it
        // started with.
        const links = state.edges.map((entry) => entry.path);
        links.forEach((path) => { path.style.opacity = '0'; });

        if (!moving.length && !arriving.length && !links.length && !leaving) return;

        // The values the animation drives are also the ones the stylesheet
        // fades, so the stylesheet is stood down for the length of it.
        const driven = [...arriving, ...links, ...(leaving ? [ghost] : [])];
        driven.forEach((el) => { el.style.transition = 'none'; });

        runAnimation((t) => {
            moving.forEach(({ group, from, to }) => {
                group.setAttribute(
                    'transform',
                    `translate(${from.x + (to.x - from.x) * t},${from.y + (to.y - from.y) * t})`,
                );
            });
            arriving.forEach((group) => { group.style.opacity = String(t); });
            links.forEach((path) => { path.style.opacity = String(t); });
            if (leaving) ghost.style.opacity = String(1 - t);
        }, ms, () => {
            // Hand every node back to its markup: an inline value left behind
            // would fight the next render, and the position belongs to the
            // transform attribute.
            moving.forEach(({ group, to }) => group.setAttribute('transform', `translate(${to.x},${to.y})`));
            driven.forEach((el) => { el.style.transition = ''; el.style.opacity = ''; });
            if (leaving) ghost.remove();
        });
    }

    function render(options) {
        const animate = Boolean(options && options.animate);
        const previous = state.positions;
        const previousCards = state.cards;
        const previousEdges = state.edgesGroup;

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

        const positions = new Map();
        placed.forEach((d) => positions.set(d.data.record.id, { x: d.card.x, y: d.card.y }));
        state.positions = positions;
        state.edgesGroup = edgesGroup;

        paint();

        if (animate) animateLayout(previous, positions, previousCards, previousEdges);
    }

    /** Is this node outside the store group the sidebar picked? */
    function filteredOut(id) {
        if (state.group === '*') return false;
        const rec = state.byId.get(id);
        return !rec || !(rec.labels || []).includes(state.group);
    }

    /** Apply the filters and the selection to what is already drawn. */
    function paint() {
        const walked = state.highlight && state.hasWalk;

        const onPath = (id) => walked && state.activeIds.has(id);

        state.cards.forEach((group, id) => {
            group.classList.toggle('is-active', onPath(id));
            group.classList.toggle('is-dim', filteredOut(id) || (walked && !onPath(id)));
            group.classList.toggle('is-selected', state.selected === id);
            group.classList.toggle('is-failed', state.failedId === id);
        });

        // A link follows what it joins: it dims when either end does, and lights
        // when BOTH ends are on the walk - which is what makes the path read as
        // one line rather than as a set of lit cards. The condition chip is part
        // of the link, so it goes with it.
        state.edges.forEach((entry) => {
            const lit = onPath(entry.source) && onPath(entry.target);
            const dim = filteredOut(entry.source) || filteredOut(entry.target) || (walked && !lit);
            entry.path.classList.toggle('is-active', lit);
            entry.path.classList.toggle('is-dim', dim);
            if (entry.chip) {
                entry.chip.classList.toggle('is-active', lit);
                entry.chip.classList.toggle('is-dim', dim);
            }
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
        'ins-self': (rec) => (rec.self_value === null || rec.self_value === undefined ? '—' : rec.self_value),
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

        // The operands: nodes this one reads, which are NOT its children and
        // so are not the cards below it.
        const operands = rec.operands || [];
        el.operands.textContent = '';
        if (!operands.length) {
            const item = document.createElement('li');
            const name = document.createElement('span');
            name.className = 'arm-name is-own';
            name.textContent = 'none — this node has no operands';
            item.appendChild(name);
            el.operands.appendChild(item);
        }
        operands.forEach((operand, index) => {
            const item = document.createElement('li');
            const key = document.createElement('span');
            key.className = 'arm-key';
            key.textContent = String(index);
            const name = document.createElement('span');
            name.className = 'arm-name';
            name.textContent = `${operand.type} — ${operand.repr}`;
            item.appendChild(key);
            item.appendChild(name);
            el.operands.appendChild(item);
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
        '--bg', '--bg-grid', '--grid-line', '--panel', '--panel-2', '--item', '--line',
        '--text', '--text-dim', '--text-faint', '--accent', '--accent-soft',
        '--ok', '--warn', '--bad',
        '--card-bg', '--card-line', '--card-repr', '--card-meta', '--card-active-ring',
        '--fam-input', '--fam-op', '--fam-action', '--fam-special', '--fam-other', '--fam-on',
        '--act-long', '--act-short', '--act-none', '--act-cancel', '--act-clear', '--act-placeholder',
        '--edge-width',
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

    // ---- The presentation sliders: what each drives, and how it reads. ----
    // A slider's units are its own (percent for a card, tenths for a stroke),
    // so each one says how to translate in and out of what it drives.
    const SLIDERS = {
        'pres-level': {
            key: 'levelGap', out: 'pres-level-out',
            fromSlider: Number, toSlider: (v) => Math.round(v), format: (v) => String(Math.round(v)),
        },
        'pres-sibling': {
            key: 'siblingGap', out: 'pres-sibling-out',
            fromSlider: Number, toSlider: (v) => Math.round(v), format: (v) => String(Math.round(v)),
        },
        'pres-width': {
            key: 'cardWidth', out: 'pres-width-out',
            fromSlider: Number, toSlider: (v) => Math.round(v), format: (v) => String(Math.round(v)),
        },
        'pres-height': {
            key: 'cardHeight', out: 'pres-height-out',
            fromSlider: Number, toSlider: (v) => Math.round(v), format: (v) => String(Math.round(v)),
        },
        'pres-edge': {
            key: 'edgeWidth', out: 'pres-edge-out',
            fromSlider: (raw) => raw / 10, toSlider: (v) => Math.round(v * 10),
            format: (v) => v.toFixed(1),
        },
        'pres-anim': {
            key: 'animSpeed', out: 'pres-anim-out',
            fromSlider: Number, toSlider: (v) => Math.round(v),
            format: (v) => `×${(v / 100).toFixed(2)}`,
            // The speed changes nothing about the drawing, so changing it does
            // not redraw one.
            redraws: false,
        },
    };

    let presentationFrame = 0;

    function syncSlider(id) {
        const spec = SLIDERS[id];
        const input = document.getElementById(id);
        if (!input) return;
        input.value = String(spec.toSlider(presentation[spec.key]));
        document.getElementById(spec.out).textContent = spec.format(presentation[spec.key]);
    }

    function applyEdgeWidth() {
        document.documentElement.style.setProperty('--edge-width', String(presentation.edgeWidth));
    }

    /**
     * Re-draw on the next frame, once.
     *
     * A drag fires an event per pixel and the drawing is rebuilt from scratch
     * every time, so the work is collapsed to one redraw per frame.
     */
    function scheduleRender() {
        if (presentationFrame) return;
        presentationFrame = requestAnimationFrame(() => {
            presentationFrame = 0;
            render();
        });
    }

    function wirePresentation() {
        for (const id in SLIDERS) {
            const spec = SLIDERS[id];
            const input = document.getElementById(id);
            if (!input) continue;
            syncSlider(id);
            input.addEventListener('input', () => {
                presentation[spec.key] = spec.fromSlider(Number(input.value));
                document.getElementById(spec.out).textContent = spec.format(presentation[spec.key]);
                applyEdgeWidth();
                if (spec.redraws !== false) scheduleRender();
            });
        }

        const reset = document.getElementById('btn-pres-reset');
        if (reset) {
            reset.addEventListener('click', () => {
                Object.assign(presentation, PRESENTATION_DEFAULTS);
                for (const id in SLIDERS) syncSlider(id);
                applyEdgeWidth();
                scheduleRender();
            });
        }

        applyEdgeWidth();
    }

    /**
     * Make every control panel foldable.
     *
     * The sidebar is a column of widgets, and the one a reader wants is rarely
     * the one taking the most room - so each panel keeps its title and puts its
     * body away. The body is wrapped here rather than in the markup, so a panel
     * is written once and a control added to it later is inside the fold by
     * default; the template says which panels start folded.
     */
    function buildPanels() {
        document.querySelectorAll('#sidebar .panel').forEach((panel) => {
            const title = panel.querySelector('.panel-title');
            if (!title || panel.dataset.foldable === 'ready') return;
            panel.dataset.foldable = 'ready';

            const body = document.createElement('div');
            body.className = 'panel-body';
            while (title.nextSibling) body.appendChild(title.nextSibling);
            panel.appendChild(body);

            const caret = document.createElement('span');
            caret.className = 'panel-caret';
            caret.setAttribute('aria-hidden', 'true');
            title.appendChild(caret);

            title.classList.add('panel-toggle');
            title.setAttribute('role', 'button');
            title.setAttribute('tabindex', '0');

            const setFolded = (folded) => {
                panel.classList.toggle('is-collapsed', folded);
                title.setAttribute('aria-expanded', String(!folded));
            };
            title.addEventListener('click', () => setFolded(!panel.classList.contains('is-collapsed')));
            title.addEventListener('keydown', (event) => {
                if (event.key !== 'Enter' && event.key !== ' ') return;
                event.preventDefault();
                setFolded(!panel.classList.contains('is-collapsed'));
            });

            setFolded(panel.dataset.collapsed === 'true');
        });
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

        buildPanels();
        buildGroupChips();
        wire();
        wirePresentation();

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
