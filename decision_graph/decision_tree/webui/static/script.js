let GLOBAL_TREE_ROOT = null;
let GLOBAL_TREE_DATA = null;
let GLOBAL_SELECTED_GROUP = "*";
let GLOBAL_VIRTUAL_LINK_DEFS = [];

// Animation durations (ms)
const ANIM_SLOW = 500; // node move animations
const ANIM_FAST = 120; // link/label fade animations (faster)

function visualizeTree(treeData) {
    GLOBAL_TREE_DATA = treeData;
    GLOBAL_VIRTUAL_LINK_DEFS = treeData.virtual_links || [];
    // Apply stored theme early so UI (export/theme button) initializes correctly
    if (typeof initTheme === 'function') initTheme();

    // Extract all unique logic groups from labels
    const allGroups = new Set();

    function collectGroups(node) {
        if (node.labels && Array.isArray(node.labels)) {
            node.labels.forEach(label => allGroups.add(label));
        }
        if (node._children) {
            node._children.forEach(collectGroups);
        }
    }

    collectGroups(treeData.root);

    const groups = ["*"].concat(Array.from(allGroups).sort());

    // Render tabs
    const tabContainer = d3.select("#logic-group-tabs");
    tabContainer.selectAll("button").remove();
    const tabs = tabContainer.selectAll("button")
        .data(groups)
        .enter()
        .append("button")
        .attr("class", d => `tab-button ${d === "*" ? "active" : ""}`)
        .attr("data-group", String)
        .text(d => d === "*" ? "All" : d)
        .on("click", function (event, group) {
            // Update active tab
            d3.selectAll(".tab-button").classed("active", false);
            d3.select(this).classed("active", true);

            GLOBAL_SELECTED_GROUP = group;
            renderFilteredTree();
        });

    // Initial render
    renderFilteredTree();
    // Add export controls (PNG/SVG) next to logic group tabs
    addExportButtons();
}

// --- Export utilities -----------------------------------------------
function getAllCSS() {
    let css = "";
    for (const sheet of document.styleSheets) {
        try {
            if (!sheet.cssRules) continue;
            for (const rule of sheet.cssRules) {
                css += rule.cssText + "\n";
            }
        } catch (e) {
            // Ignore cross-origin stylesheets
        }
    }
    return css;
}

function serializeSvgWithInlineStyles(svgNode) {
    const clone = svgNode.cloneNode(true);
    // Prepend a <style> node with collected CSS rules so the exported image
    // preserves the page styles.
    // First, inject current computed CSS variables so :root vars are resolved
    const cssVarNames = [
        '--bg', '--panel-bg', '--text-color', '--header-bg', '--header-text',
        '--tabs-bg', '--tab-active-bg', '--tab-active-text', '--muted-border'
    ];
    const comp = getComputedStyle(document.documentElement);
    let varCss = ':root {';
    cssVarNames.forEach(name => {
        const v = comp.getPropertyValue(name).trim();
        if (v) varCss += `${name}: ${v};`;
    });
    varCss += '}\n';

    const cssText = varCss + getAllCSS();
    const styleEl = document.createElement('style');
    styleEl.setAttribute('type', 'text/css');
    styleEl.innerHTML = cssText;
    clone.insertBefore(styleEl, clone.firstChild);
    // Ensure namespace
    if (!clone.getAttribute('xmlns')) {
        clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
    }
    // Ensure xlink namespace
    if (!clone.getAttribute('xmlns:xlink')) {
        clone.setAttribute('xmlns:xlink', 'http://www.w3.org/1999/xlink');
    }
    return new XMLSerializer().serializeToString(clone);
}

function downloadBlob(blob, filename) {
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 100);
}

function exportSVG() {
    const svgNode = document.querySelector('#tree-container svg');
    if (!svgNode) return alert('No SVG found to export');
    const svgText = serializeSvgWithInlineStyles(svgNode);
    const blob = new Blob([svgText], {type: 'image/svg+xml;charset=utf-8'});
    downloadBlob(blob, 'decision-tree.svg');
}

function exportPNG() {
    const svgNode = document.querySelector('#tree-container svg');
    if (!svgNode) return alert('No SVG found to export');
    const svgText = serializeSvgWithInlineStyles(svgNode);
    const svgBlob = new Blob([svgText], {type: 'image/svg+xml'});
    const url = URL.createObjectURL(svgBlob);
    const img = new Image();
    img.onload = () => {
        try {
            const vb = svgNode.viewBox && svgNode.viewBox.baseVal ? svgNode.viewBox.baseVal : null;
            const srcW = vb && vb.width ? vb.width : svgNode.clientWidth || 1000;
            const srcH = vb && vb.height ? vb.height : svgNode.clientHeight || 800;
            const scale = window.devicePixelRatio || 1;
            const canvas = document.createElement('canvas');
            canvas.width = Math.round(srcW * scale);
            canvas.height = Math.round(srcH * scale);
            const ctx = canvas.getContext('2d');
            ctx.setTransform(scale, 0, 0, scale, 0, 0);
            // Use current theme panel background for exported PNG
            const comp = getComputedStyle(document.documentElement);
            const panelBg = comp.getPropertyValue('--panel-bg') || '#ffffff';
            ctx.fillStyle = panelBg.trim() || '#ffffff';
            ctx.fillRect(0, 0, srcW, srcH);
            ctx.drawImage(img, 0, 0, srcW, srcH);
            canvas.toBlob((blob) => {
                if (blob) downloadBlob(blob, 'decision-tree.png');
            }, 'image/png');
        } finally {
            URL.revokeObjectURL(url);
        }
    };
    img.onerror = (e) => {
        URL.revokeObjectURL(url);
        alert('Failed to render SVG to PNG');
    };
    img.src = url;
}

function addExportButtons() {
    const controlsBar = d3.select('#controls-bar');
    if (!controlsBar.empty() && controlsBar.select('.right-controls').empty()) {
        const wrap = controlsBar.append('div').attr('class', 'right-controls');
        wrap.append('button').attr('id', 'export-png-btn').attr('class', 'tab-button').text('Export PNG').on('click', exportPNG);
        wrap.append('button').attr('id', 'export-svg-btn').attr('class', 'tab-button').text('Export SVG').on('click', exportSVG);
        // Theme toggle
        wrap.append('button')
            .attr('id', 'theme-toggle-btn')
            .attr('class', 'tab-button')
            .text(getCurrentTheme() === 'dark' ? 'Light' : 'Dark')
            .on('click', () => {
                toggleTheme();
                const btn = document.getElementById('theme-toggle-btn');
                if (btn) btn.textContent = getCurrentTheme() === 'dark' ? 'Light' : 'Dark';
            });
    }
}

// --- Theme utilities -------------------------------------------------
function applyTheme(theme) {
    const root = document.documentElement;
    if (theme === 'dark') root.classList.add('dark-mode');
    else root.classList.remove('dark-mode');
}

function getCurrentTheme() {
    return localStorage.getItem('dg-theme') || 'light';
}

function toggleTheme() {
    const cur = getCurrentTheme();
    const next = cur === 'dark' ? 'light' : 'dark';
    localStorage.setItem('dg-theme', next);
    applyTheme(next);
}

function initTheme() {
    const stored = getCurrentTheme();
    applyTheme(stored);
}

function buildNodeMap(root) {
    const nodeMap = new Map();
    root.each(d => nodeMap.set(d.data.id, d));
    return nodeMap;
}

function buildVirtualLinks(virtualLinkDefs, nodeMap) {
    return virtualLinkDefs
        .map(link => {
            const src = nodeMap.get(link.source);
            const tgt = nodeMap.get(link.target);
            return src && tgt ? {
                source: src,
                target: tgt,
                condition: "virtual",
                condition_type: "virtual",
                type: link.type
            } : null;
        })
        .filter(Boolean);
}

function applyTreeLayoutWithMinSpacing(root, width, height) {
    const MIN_ROW_HEIGHT = 200;   // Your existing vertical constraint

    // --- Step 1: Estimate per-node width (pixels) from label/text so spacing
    // can adapt to each node's content. Store on d.data._estWidth.
    let widths = [];
    root.each(d => {
        const text = d.data.name || d.data.id || "unnamed";
        // Estimate width from text (approximate; tweak multiplier if needed)
        const est = Math.max(40, Math.min(600, Math.round(text.length * 8 + 16)));
        d.data._estWidth = est;
        widths.push(est);
    });

    // Compute an average width to use as a base spacing unit, with sensible caps
    const avgWidth = widths.length ? Math.round(widths.reduce((a, b) => a + b, 0) / widths.length) : 80;
    const BASE_HORIZONTAL_SPACING = Math.max(50, Math.min(400, avgWidth + 40));

    // --- Step 2: Create d3.tree with a base nodeSize and a custom separation
    // function that increases spacing proportionally to node widths. nodeSize
    // is in pixel units here (we treat layout width as pixels so this remains intuitive).
    const treeLayout = d3.tree()
        .nodeSize([BASE_HORIZONTAL_SPACING, 1])
        .separation((a, b) => {
            // desired spacing (pixels) between a and b should be roughly the
            // average of their widths plus padding. separation should return a
            // multiplier relative to nodeSize[0]. Also give larger gaps when
            // nodes are from different parents to avoid collisions.
            const wa = (a && a.data && a.data._estWidth) ? a.data._estWidth : avgWidth;
            const wb = (b && b.data && b.data._estWidth) ? b.data._estWidth : avgWidth;
            const desiredPx = (wa + wb) / 2 + 20; // small padding
            const factor = desiredPx / BASE_HORIZONTAL_SPACING;
            return (a.parent === b.parent) ? Math.max(0.6, factor) : Math.max(1.2, factor * 1.2);
        });

    treeLayout(root);

    // --- Step 3: Preserve your vertical min-spacing logic (unchanged) ---
    if (root.height > 0) {
        const naturalRowHeight = height / root.height;
        if (naturalRowHeight < MIN_ROW_HEIGHT) {
            const scaleY = d3.scaleLinear()
                .domain([0, root.height])
                .range([0, root.height * MIN_ROW_HEIGHT]);
            root.each(d => {
                d.y = scaleY(d.depth);
            });
        } else {
            // If natural spacing is sufficient, use original y from layout (which is depth * 1)
            // But scale to full height for better use of space
            const scaleY = d3.scaleLinear()
                .domain([0, root.height])
                .range([0, height]);
            root.each(d => {
                d.y = scaleY(d.depth);
            });
        }
    } else {
        // Single node
        root.each(d => d.y = height / 2);
    }

    // --- Step 4: Optional – center the root horizontally if tree is narrow ---
    // Not required, but improves appearance
    let xMin = Infinity, xMax = -Infinity;
    root.each(d => {
        if (d.x < xMin) xMin = d.x;
        if (d.x > xMax) xMax = d.x;
    });
    const treeWidth = xMax - xMin;
    const offset = (width - treeWidth) / 2;
    if (isFinite(offset)) {
        root.each(d => {
            d.x += offset - xMin;
        });
    }
}

function updateVisualization(root, g, virtualLinkDefs, nodeMap, animate = true) {
    const nodes = root.descendants();

    // ── NODES ─────────────────────────────────────
    const nodeSelection = g.selectAll("g.node").data(nodes, d => d.data.id);
    const nodeEnter = nodeSelection.enter().append("g")
        .attr("class", d => `node ${d.data.type}`)
        .attr("transform", d => `translate(${d.x0},${d.y0})`)
        .on("click", toggleChildren)
        .on("mouseover", showNodeInfo)
        .on("mouseout", hideNodeInfo);

    nodeEnter.append("rect")
        .attr("class", "node-rect")
        .attr("rx", 6)
        .attr("ry", 6);

    nodeEnter.append("text")
        .attr("class", "node-text")
        .attr("text-anchor", "middle")
        .attr("dy", "0.35em");

    const nodeUpdate = nodeSelection.merge(nodeEnter);

    nodeUpdate.select("text.node-text")
        .text(d => d.data.name || d.data.id || "unnamed");

    nodeUpdate.each(function (d) {
        const text = d3.select(this).select("text").node();
        if (!text) return;
        const bbox = text.getBBox();
        const pad = 8;
        const w = Math.max(bbox.width + pad, 40);
        const h = Math.max(bbox.height + pad, 16);
        d3.select(this).select("rect")
            .attr("x", -w / 2)
            .attr("y", -h / 2)
            .attr("width", w)
            .attr("height", h);
    });

    // Apply dimming ONLY if highlight mode is ON
    const highlightToggle = document.getElementById('highlight-toggle');
    const shouldDim = highlightToggle ? highlightToggle.checked : false;
    nodeUpdate.select("rect.node-rect")
        .classed("node-rect-inactive", d => shouldDim && d.data.activated === false);

    nodeUpdate.select("text.node-text")
        .classed("node-text-inactive", d => shouldDim && d.data.activated === false);

    if (animate) {
        // Start link fades first (ANIM_FAST), then move nodes (ANIM_SLOW) so lines don't lag.
        nodeUpdate.transition().delay(ANIM_FAST).duration(ANIM_SLOW)
            .attr("transform", d => `translate(${d.x},${d.y})`);
    } else {
        nodeUpdate.attr("transform", d => `translate(${d.x},${d.y})`);
    }

    // Node exit
    if (animate) {
        nodeSelection.exit().transition().delay(ANIM_FAST).duration(ANIM_SLOW)
            .attr("transform", d => {
                const parent = d.parent || d;
                return `translate(${parent.x},${parent.y})`;
            })
            .style("opacity", 0)
            .remove();
    } else {
        nodeSelection.exit().remove();
    }

    // ── LINKS ─────────────────────────────────────
    const parentChildLinks = [];
    root.each(d => {
        if (d.children) {
            d.children.forEach(child => {
                const isLinkActivated = (d.data.activated !== false) && (child.data.activated !== false);
                parentChildLinks.push({
                    source: d,
                    target: child,
                    condition: child.data.condition_to_child || "",
                    condition_type: child.data.condition_type || "default",
                    activated: isLinkActivated
                });
            });
        }
    });

    const virtualLinks = buildVirtualLinks(virtualLinkDefs, nodeMap).map(link => {
        const srcActivated = link.source.data.activated !== false;
        const tgtActivated = link.target.data.activated !== false;
        return {
            ...link,
            activated: srcActivated && tgtActivated
        };
    });

    const allLinks = [...parentChildLinks, ...virtualLinks];

    const linkSelection = g.selectAll("path.link").data(allLinks, d => `${d.source.data.id}-${d.target.data.id}`);
    const linkEnter = linkSelection.enter().insert("path", "g")
        .attr("class", "link")
        .attr("fill", "none")
        .attr("stroke", d => d.type === "virtual_parent" ? "red" : "gray")
        .attr("stroke-width", 1)
        .attr("stroke-dasharray", d => d.type === "virtual_parent" ? "5,5" : null)
        .attr("opacity", 0);

    const linkUpdate = linkSelection.merge(linkEnter);
    linkUpdate.classed("link-inactive", d => shouldDim && d.activated === false);

    const linkGenerator = d3.linkVertical().x(d => d.x).y(d => d.y);
    if (animate) {
        linkUpdate.transition().duration(ANIM_FAST)
            .attr("d", d => linkGenerator({source: d.source, target: d.target}))
            .attr("opacity", 1);
    } else {
        linkUpdate.attr("d", d => linkGenerator({source: d.source, target: d.target})).attr("opacity", 1);
    }

    if (animate) {
        linkSelection.exit().transition().duration(ANIM_FAST)
            .attr("opacity", 0)
            .remove();
    } else {
        linkSelection.exit().remove();
    }

    // ── CONDITION LABELS ──────────────────────────
    const labelSelection = g.selectAll("g.link-condition-group").data(allLinks, d => `${d.source.data.id}-${d.target.data.id}`);
    const labelEnter = labelSelection.enter().append("g")
        .attr("class", d => `link-condition-group ${d.condition_type || "default"}`)
        .style("opacity", 0);

    labelEnter.append("rect")
        .attr("class", "link-condition-bg")
        .attr("rx", 4)
        .attr("ry", 4);

    labelEnter.append("text").attr("class", "link-condition");

    const labelUpdate = labelSelection.merge(labelEnter);
    labelUpdate.select("text.link-condition")
        .text(d => d.condition || "")
        .attr("text-anchor", "middle")
        .attr("dominant-baseline", "middle")
        .attr("font-size", "10px")
        .attr("pointer-events", "none");

    labelUpdate.each(function (d) {
        const text = d3.select(this).select("text").node();
        if (!text) return;
        const bbox = text.getBBox();
        const pad = 6;
        const w = bbox.width + pad;
        const h = bbox.height + pad;
        d3.select(this).select("rect.link-condition-bg")
            .attr("x", -w / 2)
            .attr("y", -h / 2)
            .attr("width", w)
            .attr("height", h);
        const midX = (d.source.x + d.target.x) / 2;
        const midY = (d.source.y + d.target.y) / 2;
        d3.select(this).attr("transform", `translate(${midX},${midY})`);
    });

    labelUpdate.select("rect.link-condition-bg")
        .classed("link-condition-bg-inactive", d => shouldDim && d.activated === false);

    labelUpdate.select("text.link-condition")
        .classed("link-condition-text-inactive", d => shouldDim && d.activated === false);

    if (animate) {
        labelUpdate.transition().duration(ANIM_FAST).style("opacity", 1);
        labelSelection.exit().transition().duration(ANIM_FAST).style("opacity", 0).remove();
    } else {
        labelUpdate.style("opacity", 1);
        labelSelection.exit().remove();
    }

    nodes.forEach(n => {
        n.x0 = n.x;
        n.y0 = n.y;
    });
}

function toggleChildren(event, d) {
    event.stopPropagation();

    if (d.children) {
        d._children = d.children;
        d.children = null;
    } else if (d._children) {
        d.children = d._children;
        d._children = null;
    } else {
        return; // leaf node, nothing to toggle
    }

    // Update visualization in-place without animation so the tree stays stable
    const container = d3.select("#tree-container");
    const g = container.select("svg").select("g.dg-viewport").select("g.dg-content");
    const nodeMap = new Map();
    GLOBAL_TREE_ROOT.each(n => nodeMap.set(n.data.id, n));
    // Use the stored virtual link defs if available
    const virtualLinkDefs = GLOBAL_VIRTUAL_LINK_DEFS || [];
    // Animate expand/collapse so node toggle is smooth for the user
    updateVisualization(GLOBAL_TREE_ROOT, g, virtualLinkDefs, nodeMap, true);
}

function showNodeInfo(event, d) {
    const info = d.data;
    d3.select("#info-id").text(info.id || "N/A");
    d3.select("#info-name").text(info.name || "N/A");
    d3.select("#info-repr").text(info.repr || "N/A");
    d3.select("#info-type").text(info.type || "N/A");
    d3.select("#info-labels").text(Array.isArray(info.labels) ? info.labels.join(", ") : String(info.labels || "N/A"));
    d3.select("#info-autogen").text(String(info.autogen || "N/A"));

    // Expression: fall back to name or condition if available
    let expr = "N/A";
    if (info.expression !== undefined) {
        expr = info.expression;
    } else if (info.condition_to_child) {
        expr = info.condition_to_child;
    }
    d3.select("#info-expr").text(expr);

    // Show panel
    const panel = d3.select("#node-info");
    panel.style("display", "block");
    const mouseX = event.pageX;
    const mouseY = event.pageY;
    const panelNode = panel.node();
    const panelWidth = panelNode.offsetWidth;
    const panelHeight = panelNode.offsetHeight;
    const x = Math.min(window.innerWidth - panelWidth - 10, mouseX + 10);
    const y = Math.min(window.innerHeight - panelHeight - 10, mouseY + 10);
    panel.style("left", x + "px").style("top", y + "px").style("position", "fixed");
}

function hideNodeInfo() {
    d3.select("#node-info")
        .style("display", "none");
}

function renderFilteredTree() {
    const container = d3.select("#tree-container");
    container.select("svg").remove(); // Clear previous tree

    if (!GLOBAL_TREE_DATA) return;

    const group = GLOBAL_SELECTED_GROUP;
    const treeData = GLOBAL_TREE_DATA;

    // Clone and filter tree
    function shouldInclude(node) {
        if (group === "*") return true;
        return node.labels && Array.isArray(node.labels) && node.labels.includes(group);
    }

    function cloneAndFilter(node, includeSelf) {
        const include = includeSelf || shouldInclude(node);
        const copy = {...node};

        if (node._children && node._children.length > 0) {
            const filteredChildren = node._children
                .map(child => cloneAndFilter(child, include || shouldInclude(child)))
                .filter(Boolean); // Remove nulls

            if (filteredChildren.length > 0) {
                copy._children = filteredChildren;
                copy.children = copy._children; // expanded by default
            } else {
                copy._children = [];
                copy.children = null;
            }
        } else {
            copy._children = [];
            copy.children = null;
        }

        // Only return node if it or any descendant is included
        return include || (copy.children && copy.children.length > 0) ? copy : null;
    }

    const filteredRoot = cloneAndFilter(treeData.root, shouldInclude(treeData.root));
    if (!filteredRoot) {
        container.append("div").text("No nodes match the selected logic group.");
        return;
    }

    // Proceed with layout and render (same as before)
    const margin = {top: 20, right: 20, bottom: 20, left: 20};
    const containerWidth = container.node().clientWidth;
    const containerHeight = container.node().clientHeight;

    const layoutWidth = Math.max(containerWidth, 600);
    const layoutHeight = Math.max(containerHeight, 400);

    const svg = container.append("svg");
    // Create a viewport group that will be transformed by zoom/pan, and a
    // nested content group where the tree elements live. Keeping the initial
    // translate on the content group means zooming the viewport won't clobber
    // the margin translation.
    const viewport = svg.append("g").attr("class", "dg-viewport");
    const g = viewport.append("g").attr("class", "dg-content");

    const root = d3.hierarchy(filteredRoot, d => d.children);
    GLOBAL_TREE_ROOT = root;

    const nodeMap = buildNodeMap(root);

    applyTreeLayoutWithMinSpacing(root, layoutWidth, layoutHeight);

    let xMin = Infinity, xMax = -Infinity, yMin = Infinity, yMax = -Infinity;
    root.each(d => {
        if (d.x < xMin) xMin = d.x;
        if (d.x > xMax) xMax = d.x;
        if (d.y < yMin) yMin = d.y;
        if (d.y > yMax) yMax = d.y;
    });

    const padding = 40;
    const treeWidth = xMax - xMin + 2 * padding;
    const treeHeight = yMax - yMin + 2 * padding;

    // Full canvas size includes margins
    const fullWidth = treeWidth + margin.left + margin.right;
    const fullHeight = treeHeight + margin.top + margin.bottom;

    // Configure the SVG viewBox and preserveAspectRatio. We'll use d3.zoom to
    // fit the tree into the container and allow pan/zoom rather than native scrollbars.
    svg.attr("viewBox", `0 0 ${fullWidth} ${fullHeight}`)
        .attr("preserveAspectRatio", "xMinYMin meet")
        .style("width", "100%")
        .style("height", "100%");

    // Attach zoom/pan behavior to the svg. The zoom modifies the viewport
    // group's transform, so the internal content translate is preserved.
    // We disable an upper limit on zoom to support very large trees.
    const zoom = d3.zoom()
        .scaleExtent([0.01, Number.POSITIVE_INFINITY]) // min zoom, no max cap
        .on("zoom", (event) => {
            viewport.attr("transform", event.transform);
        });

    svg.call(zoom);

    // Auto-fit the tree content into the container while preserving aspect
    // ratio. Compute a scale that fits both width and height, cap at 1 (no upscaling).
    // Center the content within the container by applying a translate.
    const cw = container.node() ? container.node().clientWidth : null;
    const ch = container.node() ? container.node().clientHeight : null;
    if (cw && ch && fullWidth > 0 && fullHeight > 0) {
        // Debug information to help diagnose placement and transforms
        try {
            console.debug('DG DEBUG -- container size', {cw, ch});
            console.debug('DG DEBUG -- full canvas', {cw, ch, fullWidth, fullHeight, margin, padding, treeWidth, treeHeight});
            console.debug('DG DEBUG -- x-range', {xMin, xMax});
            console.debug('DG DEBUG -- y-range', {yMin, yMax});
            // root is a d3.hierarchy node representing the root of this filtered tree
            const rootNode = root; // alias
            if (rootNode) {
                console.debug('DG DEBUG -- root coords', {root_x: rootNode.x, root_y: rootNode.y});
            }
        } catch (err) {
            // Fail-safe: don't break rendering if console access is restricted
            try { console.warn('DG DEBUG logging error', err); } catch (e) { /* ignore */ }
        }
        // Place the root node in the horizontal center and at 1/5 of the
        // container height vertically. Keep the initial zoom scale = 1.
        // Calculate tx,ty so that: screenX = root.x * scale + tx => centered
        const scale = fullHeight / ch; // keep initial scale at 1 for predictability
        const rootX = typeof root.x === 'number' ? root.x : 0;
        const rootY = typeof root.y === 'number' ? root.y : 0;
        const tx = (fullWidth / 2) - (rootX * scale);
        const ty = ((fullHeight * 0.1) - (rootY * scale)) / (cw / treeWidth);
        // Log computed transform values
        console.debug('DG DEBUG -- computed transform', {scale, rootX, rootY, tx, ty});
        const initialTransform = d3.zoomIdentity.translate(tx, ty).scale(scale);
        // Apply immediately (no transition) so the tree appears in place on load
        svg.call(zoom.transform, initialTransform);
        // Confirm applied transform (read back transform of viewport if possible)
        try {
            const applied = d3.zoomTransform(svg.node());
            console.debug('DG DEBUG -- applied zoom transform', applied);
        } catch (err) {
            // ignore if access fails
        }
    }

    root.each(d => {
        d.x0 = d.x;
        d.y0 = d.y;
    });

    // Initial render: do not animate layout transitions on load/reload
    updateVisualization(root, g, GLOBAL_VIRTUAL_LINK_DEFS, nodeMap, false);
}

document.addEventListener('DOMContentLoaded', () => {
    const toggle = document.getElementById('highlight-toggle');
    if (toggle) {
        toggle.addEventListener('change', function () {
            // Re-render tree on toggle
            if (typeof renderFilteredTree === 'function') {
                renderFilteredTree();
            }
        });
    }

    // Initialize theme from storage
    initTheme();
});