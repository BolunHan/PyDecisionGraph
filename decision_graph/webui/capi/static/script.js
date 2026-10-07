let GLOBAL_TREE_ROOT = null;
let GLOBAL_TREE_DATA = null;
let GLOBAL_SELECTED_GROUP = "*";
let GLOBAL_VIRTUAL_LINK_DEFS = [];
let LAST_ACTIVE_IDS = [];

const ANIM_SLOW = 500;
const ANIM_FAST = 120;
const ROW_VERTICAL_SPACE_FACTOR = 8;
let GLOBAL_HORIZONTAL_SPACING_BASE = null;
let GLOBAL_VERTICAL_SPACING = null;

const nodeInfoPinBtn = document.getElementById('node-info-pin');
const nodeInfoPanel = document.getElementById('node-info');
let nodeInfoPinned = false;
let nodeInfoDragging = false;
let nodeInfoOffset = {x: 0, y: 0};
let nodeInfoLastPos = null;
let nodeInfoCurrentNodeId = null;

function visualizeTree(treeData) {
    GLOBAL_TREE_DATA = treeData;
    GLOBAL_VIRTUAL_LINK_DEFS = treeData.virtual_links || [];
    if (typeof initTheme === 'function') initTheme();

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
            d3.selectAll(".tab-button").classed("active", false);
            d3.select(this).classed("active", true);

            GLOBAL_SELECTED_GROUP = group;
            renderFilteredTree();
        });

    renderFilteredTree();
    addExportButtons();
}

function getAllCSS() {
    let css = "";
    for (const sheet of document.styleSheets) {
        try {
            if (!sheet.cssRules) continue;
            for (const rule of sheet.cssRules) {
                css += rule.cssText + "\n";
            }
        } catch (e) {
        }
    }
    return css;
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

function getFullTreeBounds() {
    let xMin = Infinity, xMax = -Infinity, yMin = Infinity, yMax = -Infinity;
    if (!GLOBAL_TREE_ROOT) return {xMin: 0, xMax: 1000, yMin: 0, yMax: 800};
    GLOBAL_TREE_ROOT.each(d => {
        const bbox = d.data._nodeBBox || {width: 80, height: 32};

        const left = d.x - bbox.width / 2;
        const right = d.x + bbox.width / 2;
        const top = d.y - bbox.height / 2;
        const bottom = d.y + bbox.height / 2;
        if (left < xMin) xMin = left;
        if (right > xMax) xMax = right;
        if (top < yMin) yMin = top;
        if (bottom > yMax) yMax = bottom;
    });

    const pad = 40;
    return {
        xMin: xMin - pad, xMax: xMax + pad, yMin: yMin - pad, yMax: yMax + pad
    };
}

function exportSVG() {
    const svgNode = document.querySelector('#tree-container svg');
    if (!svgNode) return alert('No SVG found to export');

    const clone = svgNode.cloneNode(true);
    const bounds = getFullTreeBounds();
    clone.setAttribute('viewBox', `${bounds.xMin} ${bounds.yMin} ${bounds.xMax - bounds.xMin} ${bounds.yMax - bounds.yMin}`);

    const viewport = clone.querySelector('g.dg-viewport');
    if (viewport) viewport.removeAttribute('transform');
    const content = viewport ? viewport.querySelector('g.dg-content') : null;
    if (content) content.removeAttribute('transform');

    const cssVarNames = ['--bg', '--panel-bg', '--text-color', '--header-bg', '--header-text', '--tabs-bg', '--tab-active-bg', '--tab-active-text', '--muted-border'];
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
    if (!clone.getAttribute('xmlns')) {
        clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
    }
    const svgText = new XMLSerializer().serializeToString(clone);
    const blob = new Blob([svgText], {type: 'image/svg+xml;charset=utf-8'});
    downloadBlob(blob, 'decision-tree.svg');
}

function exportPNG() {
    const svgNode = document.querySelector('#tree-container svg');
    if (!svgNode) return alert('No SVG found to export');

    const clone = svgNode.cloneNode(true);
    const bounds = getFullTreeBounds();
    clone.setAttribute('viewBox', `${bounds.xMin} ${bounds.yMin} ${bounds.xMax - bounds.xMin} ${bounds.yMax - bounds.yMin}`);

    const viewport = clone.querySelector('g.dg-viewport');
    if (viewport) viewport.removeAttribute('transform');
    const content = viewport ? viewport.querySelector('g.dg-content') : null;
    if (content) content.removeAttribute('transform');

    const cssVarNames = ['--bg', '--panel-bg', '--text-color', '--header-bg', '--header-text', '--tabs-bg', '--tab-active-bg', '--tab-active-text', '--muted-border'];
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
    if (!clone.getAttribute('xmlns')) {
        clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
    }
    const svgText = new XMLSerializer().serializeToString(clone);
    const svgBlob = new Blob([svgText], {type: 'image/svg+xml'});
    const url = URL.createObjectURL(svgBlob);
    const img = new Image();
    img.onload = () => {
        try {
            const w = bounds.xMax - bounds.xMin;
            const h = bounds.yMax - bounds.yMin;
            const scale = window.devicePixelRatio || 1;
            const canvas = document.createElement('canvas');
            canvas.width = Math.round(w * scale);
            canvas.height = Math.round(h * scale);
            const ctx = canvas.getContext('2d');
            ctx.setTransform(scale, 0, 0, scale, 0, 0);
            const comp = getComputedStyle(document.documentElement);
            const panelBg = comp.getPropertyValue('--panel-bg') || '#ffffff';
            ctx.fillStyle = panelBg.trim() || '#ffffff';
            ctx.fillRect(0, 0, w, h);
            ctx.drawImage(img, 0, 0, w, h);
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
        wrap.append('button').attr('id', 'export-png-btn').attr('class', 'control-button').text('Export PNG').on('click', exportPNG);
        wrap.append('button').attr('id', 'export-svg-btn').attr('class', 'control-button').text('Export SVG').on('click', exportSVG);
        wrap.append('button').attr('id', 'theme-toggle-btn').attr('class', 'control-button').text(getCurrentTheme() === 'dark' ? 'Light' : 'Dark').on('click', () => {
            toggleTheme();
            const btn = document.getElementById('theme-toggle-btn');
            if (btn) btn.textContent = getCurrentTheme() === 'dark' ? 'Light' : 'Dark';
        });
    }
}

function applyTheme(theme) {
    const root = document.documentElement;
    if (theme === 'dark') root.classList.add('dark-mode'); else root.classList.remove('dark-mode');
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
                source: src, target: tgt, condition: "virtual", condition_type: "virtual", type: link.type
            } : null;
        })
        .filter(Boolean);
}

function measureNodeBBox(nodeData) {
    let svg = document.getElementById('dg-measure-svg');
    if (!svg) {
        svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        svg.setAttribute('id', 'dg-measure-svg');
        svg.setAttribute('style', 'position:absolute; left:-9999px; top:-9999px; width:0; height:0; overflow:visible;');
        document.body.appendChild(svg);
    }
    if (svg._dg_node_group) {
        svg.removeChild(svg._dg_node_group);
        svg._dg_node_group = null;
    }
    const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    g.setAttribute('class', `node ${nodeData.type || ''}`);
    const rect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
    rect.setAttribute('class', 'node-rect');
    rect.setAttribute('rx', 6);
    rect.setAttribute('ry', 6);
    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('class', 'node-text');
    text.setAttribute('text-anchor', 'middle');
    text.setAttribute('dy', '0.35em');
    text.textContent = nodeData.name || nodeData.id || 'unnamed';
    g.appendChild(text);
    svg.appendChild(g);
    const textBBox = text.getBBox();
    const pad = 8;
    const w = Math.max(textBBox.width + pad, 40);
    const h = Math.max(textBBox.height + pad, 16);
    rect.setAttribute('x', -w / 2);
    rect.setAttribute('y', -h / 2);
    rect.setAttribute('width', w);
    rect.setAttribute('height', h);
    g.insertBefore(rect, text);
    const groupBBox = g.getBBox();
    svg.removeChild(g);
    svg._dg_node_group = null;
    return {width: groupBBox.width, height: groupBBox.height};
}

function updateTreeLayout(root) {
    let nodeBBoxes = [];
    let nodeHeights = [];
    root.each(d => {
        const bbox = measureNodeBBox({
            name: d.data.name, id: d.data.id, type: d.data.type, depth: d.depth
        });
        d.data._nodeBBox = bbox;
        nodeBBoxes.push(bbox.width);
        nodeHeights.push(bbox.height);
    });
    const avgWidth = nodeBBoxes.length ? Math.round(nodeBBoxes.reduce((a, b) => a + b, 0) / nodeBBoxes.length) : 80;
    const avgHeight = nodeHeights.length ? Math.round(nodeHeights.reduce((a, b) => a + b, 0) / nodeHeights.length) : 32;

    if (GLOBAL_HORIZONTAL_SPACING_BASE === null) {
        GLOBAL_HORIZONTAL_SPACING_BASE = Math.max(50, Math.min(400, avgWidth + 40));
        GLOBAL_VERTICAL_SPACING = avgHeight * ROW_VERTICAL_SPACE_FACTOR;
    }
    const treeLayout = d3.tree()
        .nodeSize([GLOBAL_HORIZONTAL_SPACING_BASE, 1])
        .separation((a, b) => {
            const wa = (a && a.data && a.data._nodeBBox) ? a.data._nodeBBox.width : avgWidth;
            const wb = (b && b.data && b.data._nodeBBox) ? b.data._nodeBBox.width : avgWidth;
            const desiredPx = (wa + wb) / 2 + 20;
            const factor = desiredPx / GLOBAL_HORIZONTAL_SPACING_BASE;
            return (a.parent === b.parent) ? Math.max(0.5, factor) : Math.max(2, factor * 1.2);
        });
    treeLayout(root);
    root.each(d => {
        d.y = d.depth * GLOBAL_VERTICAL_SPACING;
    });
}

function updateVisualization(root, g, virtualLinkDefs, nodeMap, animate = true) {
    const nodes = root.descendants();

    const nodeSelection = g.selectAll("g.node").data(nodes, d => d.data.id);
    const nodeEnter = nodeSelection.enter().append("g")
        .attr("class", d => `node ${d.data.type}`)
        .attr("data-id", d => d.data.id)
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
        let w, h;
        const bbox = text.getBBox();
        const pad = 8;
        w = Math.max(bbox.width + pad, 40);
        h = Math.max(bbox.height + pad, 16);
        d3.select(this).select("rect")
            .attr("x", -w / 2)
            .attr("y", -h / 2)
            .attr("width", w)
            .attr("height", h);
    });

    const highlightToggle = document.getElementById('highlight-toggle');
    const shouldDim = highlightToggle ? highlightToggle.checked : false;
    nodeUpdate.select("rect.node-rect")
        .classed("node-rect-inactive", d => shouldDim && d.data.activated === false);

    nodeUpdate.select("text.node-text")
        .classed("node-text-inactive", d => shouldDim && d.data.activated === false);

    if (animate) {
        nodeUpdate.transition().delay(ANIM_FAST).duration(ANIM_SLOW)
            .attr("transform", d => `translate(${d.x},${d.y})`);
    } else {
        nodeUpdate.attr("transform", d => `translate(${d.x},${d.y})`);
    }

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

    const parentChildLinks = [];
    root.each(d => {
        if (d.children) {
            d.children.forEach(child => {
                const isLinkActivated = (d.data.activated !== false) && (child.data.activated !== false);
                parentChildLinks.push({
                    source: d, target: child, condition: child.data.condition_to_child || "", condition_type: child.data.condition_type || "default", activated: isLinkActivated
                });
            });
        }
    });

    const virtualLinks = buildVirtualLinks(virtualLinkDefs, nodeMap).map(link => {
        const srcActivated = link.source.data.activated !== false;
        const tgtActivated = link.target.data.activated !== false;
        return {
            ...link, activated: srcActivated && tgtActivated
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

    const labelSelection = g.selectAll("g.link-condition-group").data(allLinks, d => `${d.source.data.id}-${d.target.data.id}`);
    const labelEnter = labelSelection.enter().append("g")
        .attr("class", d => `link-condition-group ${d.condition_type || "default"}`)
        .attr("data-link", d => `${d.source.data.id}-${d.target.data.id}`)
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
        return;
    }

    const container = d3.select("#tree-container");
    const g = container.select("svg").select("g.dg-viewport").select("g.dg-content");
    const nodeMap = new Map();
    GLOBAL_TREE_ROOT.each(n => nodeMap.set(n.data.id, n));
    const virtualLinkDefs = GLOBAL_VIRTUAL_LINK_DEFS || [];
    updateVisualization(GLOBAL_TREE_ROOT, g, virtualLinkDefs, nodeMap, true);
}

function showNodeInfo(event, d) {
    const info = d.data;
    d3.select("#info-id").text(info.id || "N/A");
    d3.select("#info-name").text(info.name || "N/A");
    d3.select("#info-repr").text(info.repr || "N/A");
    d3.select("#info-type").text(info.type || "N/A");
    d3.select("#info-labels").text(Array.isArray(info.labels) ? info.labels.join(", ") : String(info.labels || "N/A"));
    const autogenElem = d3.select("#info-autogen");
    autogenElem.classed("autogen-true", false).classed("autogen-false", false);
    if (info.autogen === undefined) {
        autogenElem.text("N/A");
    } else if (info.autogen) {
        autogenElem.text("True").classed("autogen-true", true);
    } else {
        autogenElem.text("False").classed("autogen-false", true);
    }
    let expr = "N/A";
    if (info.expression !== undefined) {
        expr = info.expression;
    } else if (info.condition_to_child) {
        expr = info.condition_to_child;
    }
    d3.select("#info-expr").text(expr);

    const panel = d3.select("#node-info");
    panel.style("display", "block");
    nodeInfoCurrentNodeId = info.id;

    if (!nodeInfoPinned && !nodeInfoDragging && !isMouseOverNodeInfo()) {
        const mouseX = event.pageX;
        const mouseY = event.pageY;
        const panelNode = panel.node();
        const panelWidth = panelNode.offsetWidth;
        const panelHeight = panelNode.offsetHeight;
        const container = document.getElementById('tree-container');
        const containerRect = container.getBoundingClientRect();
        let x = mouseX + 10;
        let y = mouseY + 10;
        if (x + panelWidth > containerRect.right || y + panelHeight > containerRect.bottom) {
            x = containerRect.right - panelWidth - 10;
            y = containerRect.top + 10;
        }
        panel.style("left", x + "px").style("top", y + "px").style("position", "fixed");
        nodeInfoLastPos = {x, y};
    }
}

function hideNodeInfo() {
    if (nodeInfoPinned || isMouseOverNodeInfo() || nodeInfoDragging) return;
    d3.select("#node-info").style("display", "none");
    nodeInfoCurrentNodeId = null;
}

function isMouseOverNodeInfo() {
    const panel = document.getElementById('node-info');
    const rect = panel.getBoundingClientRect();
    const x = window.event ? window.event.clientX : 0;
    const y = window.event ? window.event.clientY : 0;
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

function renderFilteredTree(include_all_children = false) {
    const container = d3.select("#tree-container");
    container.select("svg").remove();

    if (!GLOBAL_TREE_DATA) return;

    const group = GLOBAL_SELECTED_GROUP;
    const treeData = GLOBAL_TREE_DATA;

    function shouldInclude(node) {
        if (group === "*") return true;
        return node.labels && Array.isArray(node.labels) && node.labels.includes(group);
    }

    function cloneAndFilter(node, includeSelf) {
        const include = includeSelf || shouldInclude(node);
        const copy = {...node};

        if (node._children && node._children.length > 0) {
            const filteredChildren = node._children
                .map(child => include_all_children ? cloneAndFilter(child, include || shouldInclude(child)) : cloneAndFilter(child, false))
                .filter(Boolean);

            if (filteredChildren.length > 0) {
                copy._children = filteredChildren;
                copy.children = copy._children;
            } else {
                copy._children = [];
                copy.children = null;
            }
        } else {
            copy._children = [];
            copy.children = null;
        }

        return include || (copy.children && copy.children.length > 0) ? copy : null;
    }

    const filteredRoot = cloneAndFilter(treeData.root, shouldInclude(treeData.root));
    if (!filteredRoot) {
        container.append("div").text("No nodes match the selected logic group.");
        return;
    }

    const svg = container.append("svg");
    const viewport = svg.append("g").attr("class", "dg-viewport");
    const g = viewport.append("g").attr("class", "dg-content");

    const root = d3.hierarchy(filteredRoot, d => d.children);
    GLOBAL_TREE_ROOT = root;

    const nodeMap = buildNodeMap(root);

    updateTreeLayout(root);

    const zoom = d3.zoom()
        .scaleExtent([0.0, Number.POSITIVE_INFINITY])
        .on("zoom", (event) => {
            viewport.attr("transform", event.transform);
        });

    svg.call(zoom);

    const cw = container.node() ? container.node().clientWidth : null;
    const ch = container.node() ? container.node().clientHeight : null;
    if (cw && ch) {
        const rootX = typeof root.x === 'number' ? root.x : 0;
        const rootY = typeof root.y === 'number' ? root.y : 0;
        const tx = (cw / 2) - rootX;
        const ty = ((ch * 0.1) - rootY);
        const initialTransform = d3.zoomIdentity.translate(tx, ty).scale(1);
        svg.call(zoom.transform, initialTransform);
    }

    root.each(d => {
        d.x0 = d.x;
        d.y0 = d.y;
    });

    updateVisualization(root, g, GLOBAL_VIRTUAL_LINK_DEFS, nodeMap, false);
}

function applyActivationDiff(diff) {
    diff.added.forEach(function (id) {
        let rectSel = d3.select(`g.node[data-id='${id}'] rect.node-rect`);
        let textSel = d3.select(`g.node[data-id='${id}'] text.node-text`);
        if (!rectSel.empty()) {
            rectSel.classed('node-rect-inactive', false);
        }
        if (!textSel.empty()) {
            textSel.classed('node-text-inactive', false);
        }
    });
    diff.removed.forEach(function (id) {
        let rectSel = d3.select(`g.node[data-id='${id}'] rect.node-rect`);
        let textSel = d3.select(`g.node[data-id='${id}'] text.node-text`);
        if (!rectSel.empty()) {
            rectSel.classed('node-rect-inactive', true);
        }
        if (!textSel.empty()) {
            textSel.classed('node-text-inactive', true);
        }
    });

    d3.selectAll('path.link').each(function (d) {
        if (!d || !d.source || !d.target) return;
        let srcRect = d3.select(`g.node[data-id='${d.source.data.id}'] rect.node-rect`);
        let tgtRect = d3.select(`g.node[data-id='${d.target.data.id}'] rect.node-rect`);
        const srcActive = !srcRect.empty() && !srcRect.classed('node-rect-inactive');
        const tgtActive = !tgtRect.empty() && !tgtRect.classed('node-rect-inactive');
        const linkActive = srcActive && tgtActive;
        d3.select(this).classed('link-inactive', !linkActive);
        d3.select(this).classed('link-active', linkActive);

        let labelGroup = d3.select(`g.link-condition-group[data-link='${d.source.data.id}-${d.target.data.id}']`);
        let labelBg = labelGroup.select('rect.link-condition-bg');
        let labelText = labelGroup.select('text.link-condition');
        if (!labelBg.empty()) {
            labelBg.classed('link-condition-bg-inactive', !linkActive);
        }
        if (!labelText.empty()) {
            labelText.classed('link-condition-text-inactive', !linkActive);
        }
    });
}

function updateHighlightClasses() {
    const highlightToggle = document.getElementById('highlight-toggle');
    const shouldDim = highlightToggle ? highlightToggle.checked : false;
    LAST_ACTIVE_IDS.length = 0;
    d3.selectAll('g.node').each(function (d) {
        if (d && d.data) {
            const rectSel = d3.select(this).select('rect.node-rect');
            const textSel = d3.select(this).select('text.node-text');
            if (d.data.activated === false) {
                rectSel.classed('node-rect-inactive', true);
                textSel.classed('node-text-inactive', true);
            } else {
                rectSel.classed('node-rect-inactive', false);
                textSel.classed('node-text-inactive', false);
                LAST_ACTIVE_IDS.push(d.data.id);
            }
        }
    });

    d3.selectAll('rect.node-rect')
        .classed('node-rect-inactive', function () {
            const d = d3.select(this.parentNode).datum();
            if (!d || !d.data) return false;
            return shouldDim && d.data.activated === false;
        });
    d3.selectAll('text.node-text')
        .classed('node-text-inactive', function () {
            const d = d3.select(this.parentNode).datum();
            if (!d || !d.data) return false;
            return shouldDim && d.data.activated === false;
        });
    d3.selectAll('path.link')
        .classed('link-inactive', function () {
            const d = d3.select(this).datum();
            if (!d) return false;
            return shouldDim && d.activated === false;
        })
        .classed('link-active', function () {
            const d = d3.select(this).datum();
            if (!d) return false;
            return shouldDim && d.activated !== false;
        });
    d3.selectAll('rect.link-condition-bg')
        .classed('link-condition-bg-inactive', function () {
            const d = d3.select(this.parentNode).datum();
            if (!d) return false;
            return shouldDim && d.activated === false;
        });
    d3.selectAll('text.link-condition')
        .classed('link-condition-text-inactive', function () {
            const d = d3.select(this.parentNode).datum();
            if (!d) return false;
            return shouldDim && d.activated === false;
        });
}

function pollActiveNodes() {
    const highlightToggle = document.getElementById('highlight-toggle');
    const watchToggle = document.getElementById('watch-toggle');
    if ((highlightToggle && !highlightToggle.checked) || (watchToggle && !watchToggle.checked)) {

        return;
    }
    fetch('/api/active_nodes')
        .then(response => response.json())
        .then(data => {
            if (!data || !Array.isArray(data.active_ids)) return;
            const newActive = data.active_ids;
            const added = newActive.filter(id => !LAST_ACTIVE_IDS.includes(id));
            const removed = LAST_ACTIVE_IDS.filter(id => !newActive.includes(id));
            if (added.length > 0 || removed.length > 0) {
                applyActivationDiff({added, removed});
            }
            LAST_ACTIVE_IDS = newActive;
        })
        .catch(err => {
            console.warn('Failed to fetch active nodes:', err);
        });
}

function setActiveIds(ids) {
    if (!Array.isArray(ids)) return;
    const idSet = new Set(ids.map(String));

    // Update the underlying GLOBAL_TREE_DATA structure (if present)
    function recurseUpdate(node) {
        if (!node || !node.id) return;
        if (idSet.has(String(node.id))) {
            // mark as active: remove explicit false flag
            if (node.hasOwnProperty('activated')) delete node.activated;
        } else {
            node.activated = false;
        }
        if (node._children && Array.isArray(node._children)) node._children.forEach(recurseUpdate);
        if (node.children && Array.isArray(node.children)) node.children.forEach(recurseUpdate);
    }

    if (GLOBAL_TREE_DATA && GLOBAL_TREE_DATA.root) {
        recurseUpdate(GLOBAL_TREE_DATA.root);
    }

    // Update currently rendered nodes' bound data (d.data) so UI reflects change
    try {
        d3.selectAll('g.node').each(function (d) {
            if (!d || !d.data || !d.data.id) return;
            if (idSet.has(String(d.data.id))) {
                if (d.data.hasOwnProperty('activated')) delete d.data.activated;
            } else {
                d.data.activated = false;
            }
        });
    } catch (e) {
        console.warn('Failed to update rendered node data:', e);
    }

    // Refresh highlight classes
    try {
        updateHighlightClasses();
    } catch (e) {
        console.warn('Failed to update highlight classes:', e);
    }
}

async function from_clipboard() {
    const inputEl = document.getElementById('from-clipboard-input');
    let raw = inputEl && inputEl.value ? inputEl.value.trim() : '';

    if (!raw) {
        if (!navigator.clipboard || !navigator.clipboard.readText) {
            alert('Clipboard API not available and input is empty');
            return;
        }
        try {
            raw = await navigator.clipboard.readText();
        } catch (e) {
            alert('Failed to read clipboard: ' + (e && e.message ? e.message : e));
            return;
        }
    }

    let parsed;
    try {
        parsed = JSON.parse(raw);
    } catch (e) {
        alert('Failed to parse JSON from input/clipboard: ' + (e && e.message ? e.message : e));
        return;
    }

    if (!Array.isArray(parsed)) {
        alert('Parsed content is not a JSON array of UIDs');
        return;
    }

    // Apply the active ids
    try {
        setActiveIds(parsed);
    } catch (e) {
        console.error('Error applying active ids:', e);
    }
}

if (typeof window.with_eval !== 'undefined' && window.with_eval) {
    document.addEventListener('DOMContentLoaded', function () {
        var controlsBar = document.getElementById('controls-bar');

        if (controlsBar && !document.getElementById('from-clipboard-control')) {
            var fc = document.createElement('div');
            fc.id = 'from-clipboard-control';
            fc.innerHTML = `
                <button id="from-clipboard-btn" class="control-button from-clipboard-btn">Load Active</button>
                <input id="from-clipboard-input" class="from-clipboard-input" type="text" placeholder='["uid1","uid2",...]'>
            `;
            controlsBar.prepend(fc);

            // Wire up events after element creation
            var inputEl = document.getElementById('from-clipboard-input');
            var btn = document.getElementById('from-clipboard-btn');
            if (btn) {
                btn.addEventListener('click', function () {
                    // call the async function; ignore returned promise
                    from_clipboard();
                });
            }
            if (inputEl) {
                inputEl.addEventListener('keydown', function (e) {
                    if (e.key === 'Enter') {
                        e.preventDefault();
                        from_clipboard();
                    }
                });
            }
        }

        if (controlsBar && !document.getElementById('highlight-control')) {
            var div = document.createElement('div');
            div.id = 'highlight-control';
            div.innerHTML = `<label>
                <span>Highlight Active Path</span>
                <input type="checkbox" id="highlight-toggle" checked>
                <span class="highlight-switch-slider"></span>
            </label>`;
            controlsBar.prepend(div);
        }
    });
}

if (typeof window.with_watch !== 'undefined' && window.with_watch) {
    document.addEventListener('DOMContentLoaded', function () {
        var controlsBar = document.getElementById('controls-bar');
        if (controlsBar && !document.getElementById('watch-control')) {
            var div = document.createElement('div');
            div.id = 'watch-control';
            div.innerHTML = `<label>
                <span>Watch Active Path</span>
                <input type="checkbox" id="watch-toggle" checked>
                <span class="watch-switch-slider"></span>
            </label>`;
            controlsBar.prepend(div);
        }
    });

    setInterval(pollActiveNodes, 500);
}

document.addEventListener('DOMContentLoaded', function () {
    const highlight_toggle = document.getElementById('highlight-toggle');
    if (highlight_toggle) {
        highlight_toggle.addEventListener('change', function () {
            updateHighlightClasses();
        });
    }

    updateHighlightClasses();

    initTheme();
});

document.addEventListener('keydown', function (e) {
    if (e.code === 'Space' || e.key === ' ') {
        const nodeInfoPanel = document.getElementById('node-info');
        const pinBtn = document.getElementById('node-info-pin');
        if (nodeInfoPanel) {
            if (nodeInfoPinned) {
                nodeInfoPinned = false;
                if (pinBtn) {
                    pinBtn.classList.remove('active');
                    pinBtn.setAttribute('title', 'Pin');
                }
                nodeInfoPanel.style.display = 'none';
            } else {
                nodeInfoPanel.style.display = 'block';
                nodeInfoPinned = true;
                if (pinBtn) {
                    pinBtn.classList.add('active');
                    pinBtn.setAttribute('title', 'Unpin');
                }
            }
        }
        e.preventDefault();
    }
});

if (nodeInfoPinBtn) {
    nodeInfoPinBtn.addEventListener('click', function (e) {
        nodeInfoPinned = !nodeInfoPinned;
        this.classList.toggle('active', nodeInfoPinned);
        if (nodeInfoPinned) {
            this.setAttribute('title', 'Unpin');
        } else {
            this.setAttribute('title', 'Pin');
        }
    });
}

if (nodeInfoPanel) {
    nodeInfoPanel.addEventListener('mousedown', function (e) {
        if (e.target.id === 'node-info-pin') return;
        nodeInfoDragging = true;
        nodeInfoPanel.classList.add('dragging');
        nodeInfoOffset.x = e.clientX - nodeInfoPanel.getBoundingClientRect().left;
        nodeInfoOffset.y = e.clientY - nodeInfoPanel.getBoundingClientRect().top;
        document.body.style.userSelect = 'none';
    });
    document.addEventListener('mousemove', function (e) {
        if (!nodeInfoDragging) return;
        let x = e.clientX - nodeInfoOffset.x;
        let y = e.clientY - nodeInfoOffset.y;
        nodeInfoPanel.style.left = x + 'px';
        nodeInfoPanel.style.top = y + 'px';
        nodeInfoLastPos = {x, y};
    });
    document.addEventListener('mouseup', function (e) {
        if (nodeInfoDragging) {
            nodeInfoDragging = false;
            nodeInfoPanel.classList.remove('dragging');
            document.body.style.userSelect = '';
        }
    });
    nodeInfoPanel.addEventListener('mouseenter', function (e) {
        if (nodeInfoCurrentNodeId) {
            d3.select('#node-info').style('display', 'block');
        }
    });
    nodeInfoPanel.addEventListener('mouseleave', function (e) {
        if (!nodeInfoPinned && !nodeInfoDragging) {
            d3.select('#node-info').style('display', 'none');
            nodeInfoCurrentNodeId = null;
        }
    });
}
