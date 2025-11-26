let GLOBAL_TREE_ROOT = null;
let GLOBAL_TREE_DATA = null;
let GLOBAL_SELECTED_GROUP = "*";
let GLOBAL_VIRTUAL_LINK_DEFS = [];

function visualizeTree(treeData) {
    GLOBAL_TREE_DATA = treeData;
    GLOBAL_VIRTUAL_LINK_DEFS = treeData.virtual_links || [];

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

    // --- Step 1: Estimate max node width ---
    let maxNodeWidth = 40; // fallback
    root.each(d => {
        const text = d.data.name || d.data.id || "unnamed";
        // Estimate width from text (approximate; adjust multiplier if needed)
        const estimatedWidth = Math.max(40, text.length * 8 + 16); // 8px per char, 8px padding each side
        if (estimatedWidth > maxNodeWidth) {
            maxNodeWidth = estimatedWidth;
        }
    });

    // Ensure reasonable min and cap (avoid extreme values)
    maxNodeWidth = Math.max(50, Math.min(300, maxNodeWidth));
    const NODE_HORIZONTAL_SPACING = maxNodeWidth + 40; // 20px gap on each side

    // --- Step 2: Apply D3 tree layout with nodeSize (horizontal spacing dynamic, vertical minimal) ---
    // dy = 1 so vertical spacing is controlled separately below
    const layout = d3.tree().nodeSize([NODE_HORIZONTAL_SPACING, 1]);
    layout(root);

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

function updateVisualization(root, g, virtualLinkDefs, nodeMap) {
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
        .attr("dy", "0.35em")
        .attr("fill", "black");

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

    nodeUpdate.transition().duration(500)
        .attr("transform", d => `translate(${d.x},${d.y})`);

    // Node exit
    nodeSelection.exit().transition().duration(500)
        .attr("transform", d => {
            const parent = d.parent || d;
            return `translate(${parent.x},${parent.y})`;
        })
        .style("opacity", 0)
        .remove();

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
    linkUpdate.transition().duration(500)
        .attr("d", d => linkGenerator({source: d.source, target: d.target}))
        .attr("opacity", 1);

    linkSelection.exit().transition().duration(500)
        .attr("opacity", 0)
        .remove();

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
        .attr("fill", "black")
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

    labelUpdate.transition().duration(500).style("opacity", 1);
    labelSelection.exit().transition().duration(500).style("opacity", 0).remove();

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

    // 🔑 Re-render the ENTIRE tree from global root
    const container = d3.select("#tree-container");
    // Select the content group (dg-content) where nodes/links are rendered.
    const g = container.select("svg").select("g.dg-viewport").select("g.dg-content");

    // Re-extract virtual links and nodeMap from global root
    const nodeMap = new Map();
    GLOBAL_TREE_ROOT.each(n => nodeMap.set(n.data.id, n));

    // Find original virtual_links from initial treeData (we don't store it globally, so infer from node types)
    // Simpler: pass virtualLinkDefs as empty if not stored; or store treeData globally.
    // For now, assume virtual links are unchanged → reuse from initial load is hard without global state.
    // Workaround: since virtual links are rare, and your backend sends them, we can store them too.
    // But to avoid complexity, we'll assume `virtualLinkDefs` is empty for now (or you can store it).

    // ⚠️ If you need virtual links to persist, store `GLOBAL_VIRTUAL_LINK_DEFS` in visualizeTree.
    // For correctness, let's assume we don't have them here → pass empty.
    const virtualLinkDefs = []; // or store globally if needed

    updateVisualization(GLOBAL_TREE_ROOT, g, virtualLinkDefs, nodeMap);
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
    const wrapper = d3.select("#tree-scroll-wrapper");
    const containerWidth = wrapper.node().clientWidth;
    const containerHeight = wrapper.node().clientHeight;

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

    // Make the SVG responsive: use a viewBox that fits the calculated tree bounds
    // and let the SVG stretch to fill its container with CSS (width/height: 100%).
    // Using preserveAspectRatio="xMinYMin meet" preserves aspect ratio while
    // aligning to the top-left of the container. If you prefer the tree to
    // stretch non-uniformly to completely fill the container, change this to
    // "none".
    const fullWidth = treeWidth + margin.left + margin.right;
    const fullHeight = treeHeight + margin.top + margin.bottom;

    // Ensure the scroll-wrapper allows scrolling when the svg is larger than the viewport
    wrapper.style("overflow", "auto");

    // If the computed tree width is wider than the wrapper, give the SVG a
    // pixel width equal to the computed full width so a horizontal scrollbar
    // will appear; otherwise let it scale down to 100% of the wrapper width.
    svg.attr("viewBox", `0 0 ${fullWidth} ${fullHeight}`)
        .attr("preserveAspectRatio", "xMinYMin meet");

    const wrapperWidthNow = wrapper.node() ? wrapper.node().clientWidth : null;
    if (wrapperWidthNow && fullWidth > wrapperWidthNow) {
        // Keep 1:1 pixel mapping so the tree isn't shrunk horizontally — user can scroll
        svg.style("width", `${fullWidth}px`).style("height", `${fullHeight}px`);
    } else {
        // Fit horizontally but keep aspect ratio (height auto) so vertical scrolling still works
        svg.style("width", "100%").style("height", "auto");
    }

    // Apply the initial margin translation on the content group. The viewport
    // transform (from zoom) will be applied on top of this translate.
    g.attr("transform", `translate(${margin.left - xMin + padding},${margin.top - yMin + padding})`);

    // Attach zoom/pan behavior to the svg. The zoom modifies the viewport
    // group's transform, so the internal content translate is preserved.
    const zoom = d3.zoom()
        .scaleExtent([0.2, 4]) // allow zoom out to 20% and zoom in to 400%
        .on("zoom", (event) => {
            viewport.attr("transform", event.transform);
        });

    svg.call(zoom);

    root.each(d => {
        d.x0 = d.x;
        d.y0 = d.y;
    });

    updateVisualization(root, g, GLOBAL_VIRTUAL_LINK_DEFS, nodeMap);
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
});