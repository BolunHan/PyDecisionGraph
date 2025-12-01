let GLOBAL_TREE_ROOT = null;
let GLOBAL_TREE_DATA = null;
let GLOBAL_SELECTED_GROUP = "*";
let GLOBAL_VIRTUAL_LINK_DEFS = [];

const ANIM_SLOW = 500;
const ANIM_FAST = 120;

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

function serializeSvgWithInlineStyles(svgNode) {
    const clone = svgNode.cloneNode(true);
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
    if (!clone.getAttribute('xmlns')) {
        clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
    }
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

function measureTextWidth(text) {
    try {
        if (typeof document === 'undefined') {
            return Math.max(40, Math.round((text ? text.length : 0) * 8 + 16));
        }

        let svg = document.getElementById('dg-measure-svg');
        if (!svg) {
            svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
            svg.setAttribute('id', 'dg-measure-svg');
            svg.setAttribute('style', 'position:absolute; left:-9999px; top:-9999px; width:0; height:0; overflow:visible;');
            document.body.appendChild(svg);
        }

        if (!svg._dg_text_el) {
            const t = document.createElementNS('http://www.w3.org/2000/svg', 'text');
            t.setAttribute('class', 'node-text');
            t.setAttribute('x', 0);
            t.setAttribute('y', 0);
            svg.appendChild(t);
            svg._dg_text_el = t;
        }

        const textEl = svg._dg_text_el;
        textEl.textContent = text == null ? '' : String(text);
        const bbox = textEl.getBBox();
        const pad = 16;
        return Math.max(40, Math.round(bbox.width + pad));
    } catch (err) {
        return Math.max(40, Math.round((text ? text.length : 0) * 8 + 16));
    }
}

function applyTreeLayoutWithMinSpacing(root, width, height) {
    const MIN_ROW_HEIGHT = 200;

    let widths = [];
    root.each(d => {
        const txt = d.data.name || d.data.id || "unnamed";
        const measured = measureTextWidth(txt);
        const est = Math.max(40, Math.min(800, measured));
        d.data._estWidth = est;
        widths.push(est);
    });

    const avgWidth = widths.length ? Math.round(widths.reduce((a, b) => a + b, 0) / widths.length) : 80;
    const BASE_HORIZONTAL_SPACING = Math.max(50, Math.min(400, avgWidth + 40));

    const treeLayout = d3.tree()
        .nodeSize([BASE_HORIZONTAL_SPACING, 1])
        .separation((a, b) => {
            const wa = (a && a.data && a.data._estWidth) ? a.data._estWidth : avgWidth;
            const wb = (b && b.data && b.data._estWidth) ? b.data._estWidth : avgWidth;
            const desiredPx = (wa + wb) / 2 + 20;
            const factor = desiredPx / BASE_HORIZONTAL_SPACING;
            return (a.parent === b.parent) ? Math.max(0.6, factor) : Math.max(1.2, factor * 1.2);
        });

    treeLayout(root);

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
            const scaleY = d3.scaleLinear()
                .domain([0, root.height])
                .range([0, height]);
            root.each(d => {
                d.y = scaleY(d.depth);
            });
        }
    } else {
        root.each(d => d.y = height / 2);
    }

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
        let w, h;
        if (d.depth === 0) {
            w = 94;
            h = 32;
        } else {
            const bbox = text.getBBox();
            const pad = 8;
            w = Math.max(bbox.width + pad, 40);
            h = Math.max(bbox.height + pad, 16);
        }
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
    d3.select("#info-autogen").text(String(info.autogen || "N/A"));

    let expr = "N/A";
    if (info.expression !== undefined) {
        expr = info.expression;
    } else if (info.condition_to_child) {
        expr = info.condition_to_child;
    }
    d3.select("#info-expr").text(expr);

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
                .map(child => cloneAndFilter(child, include || shouldInclude(child)))
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

    const margin = {top: 0, right: 0, bottom: 0, left: 0};
    const containerWidth = container.node().clientWidth;
    const containerHeight = container.node().clientHeight;

    const svg = container.append("svg");
    const viewport = svg.append("g").attr("class", "dg-viewport");
    const g = viewport.append("g").attr("class", "dg-content");

    const root = d3.hierarchy(filteredRoot, d => d.children);
    GLOBAL_TREE_ROOT = root;

    const nodeMap = buildNodeMap(root);

    applyTreeLayoutWithMinSpacing(root, containerWidth, containerHeight);

    let xMin = Infinity, xMax = -Infinity, yMin = Infinity, yMax = -Infinity;
    root.each(d => {
        if (d.x < xMin) xMin = d.x;
        if (d.x > xMax) xMax = d.x;
        if (d.y < yMin) yMin = d.y;
        if (d.y > yMax) yMax = d.y;
    });

    const padding = 0;
    const treeWidth = xMax - xMin + 2 * padding;
    const treeHeight = yMax - yMin + 2 * padding;

    const fullWidth = treeWidth + margin.left + margin.right;
    const fullHeight = treeHeight + margin.top + margin.bottom;

    svg.attr("viewBox", `0 0 ${fullWidth} ${fullHeight}`)
        .attr("preserveAspectRatio", "xMinYMin meet")
        .style("width", "100%")
        .style("height", "100%");

    const zoom = d3.zoom()
        .scaleExtent([0.0, Number.POSITIVE_INFINITY])
        .on("zoom", (event) => {
            viewport.attr("transform", event.transform);
        });

    svg.call(zoom);

    const cw = container.node() ? container.node().clientWidth : null;
    const ch = container.node() ? container.node().clientHeight : null;
    if (cw && ch && fullWidth > 0 && fullHeight > 0) {
        const scale = fullHeight / ch;
        const rootX = typeof root.x === 'number' ? root.x : 0;
        const rootY = typeof root.y === 'number' ? root.y : 0;
        const tx = (fullWidth / 2) - (rootX * scale);
        const ty = ((fullHeight * 0.1) - (rootY * scale)) / Math.max(cw / treeWidth, ch / treeHeight);
        const initialTransform = d3.zoomIdentity.translate(tx, ty).scale(scale);
        svg.call(zoom.transform, initialTransform);
    }

    root.each(d => {
        d.x0 = d.x;
        d.y0 = d.y;
    });

    updateVisualization(root, g, GLOBAL_VIRTUAL_LINK_DEFS, nodeMap, false);
}

function updateHighlightClasses() {
    const highlightToggle = document.getElementById('highlight-toggle');
    const shouldDim = highlightToggle ? highlightToggle.checked : false;
    d3.selectAll('rect.node-rect')
        .classed('node-rect-inactive', function() {
            const d = d3.select(this.parentNode).datum();
            if (!d || !d.data) return false;
            return shouldDim && d.data.activated === false;
        });
    d3.selectAll('text.node-text')
        .classed('node-text-inactive', function() {
            const d = d3.select(this.parentNode).datum();
            if (!d || !d.data) return false;
            return shouldDim && d.data.activated === false;
        });
    d3.selectAll('path.link')
        .classed('link-inactive', function() {
            const d = d3.select(this).datum();
            if (!d) return false;
            return shouldDim && d.activated === false;
        })
        .classed('link-active', function() {
            const d = d3.select(this).datum();
            if (!d) return false;
            return shouldDim && d.activated !== false;
        });
    d3.selectAll('rect.link-condition-bg')
        .classed('link-condition-bg-inactive', function() {
            const d = d3.select(this.parentNode).datum();
            if (!d) return false;
            return shouldDim && d.activated === false;
        });
    d3.selectAll('text.link-condition')
        .classed('link-condition-text-inactive', function() {
            const d = d3.select(this.parentNode).datum();
            if (!d) return false;
            return shouldDim && d.activated === false;
        });
}

document.addEventListener('DOMContentLoaded', () => {
    const toggle = document.getElementById('highlight-toggle');
    if (toggle) {
        toggle.addEventListener('change', function () {
            updateHighlightClasses();
        });
    }

    initTheme();
});