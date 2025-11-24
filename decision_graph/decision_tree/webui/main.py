"""
Web UI for visualizing decision trees with LogicGroups
"""
import json
import webbrowser
import threading
import time
from typing import Dict, Any, List, Optional
from flask import Flask, render_template_string, request, jsonify
from decision_graph.decision_tree.capi.c_abc import LogicNode, LogicGroup


def create_app(node: LogicNode):
    """Create Flask app for visualizing the decision tree."""
    
    app = Flask(__name__)
    
    # HTML template for the visualization
    html_template = """
<!DOCTYPE html>
<html>
<head>
    <title>Decision Tree Visualizer</title>
    <script src="https://d3js.org/d3.v7.min.js"></script>
    <style>
        body {
            margin: 0;
            padding: 20px;
            font-family: Arial, sans-serif;
            background-color: #f5f5f5;
        }
        
        #tree-container {
            width: 100%;
            height: 80vh;
            border: 1px solid #ccc;
            background-color: white;
            overflow: auto;
        }
        
        .node {
            cursor: pointer;
        }
        
        .node circle {
            fill: #fff;
            stroke: steelblue;
            stroke-width: 3px;
        }
        
        .node text {
            font: 12px sans-serif;
        }
        
        .link {
            fill: none;
            stroke: #ccc;
            stroke-width: 2px;
        }
        
        .logic-group {
            fill: #ff9999;
        }
        
        .logic-node {
            fill: #99ccff;
        }
        
        .node-label {
            font-size: 10px;
            fill: #333;
        }
        
        .controls {
            margin-bottom: 10px;
        }
        
        button {
            margin-right: 10px;
            padding: 5px 10px;
        }
    </style>
</head>
<body>
    <div class="controls">
        <button onclick="zoomIn()">Zoom In</button>
        <button onclick="zoomOut()">Zoom Out</button>
        <button onclick="resetZoom()">Reset Zoom</button>
    </div>
    <div id="tree-container"></div>

    <script>
        // Data from backend
        const treeData = {{ tree_data | safe }};
        
        // Set up dimensions
        const width = document.getElementById('tree-container').clientWidth;
        const height = document.getElementById('tree-container').clientHeight;
        
        // Create SVG container
        const svg = d3.select("#tree-container")
            .append("svg")
            .attr("width", width)
            .attr("height", height)
            .call(d3.zoom().on("zoom", (event) => {
                g.attr("transform", event.transform);
            }))
            .append("g");
        
        // Create group for the tree
        const g = svg.append("g");
        
        // Create a tree layout
        const root = d3.hierarchy(treeData);
        const treeLayout = d3.tree().size([height - 100, width - 100]);
        
        // Compute the tree layout
        treeLayout(root);
        
        // Links
        const link = g.selectAll(".link")
            .data(root.links())
            .enter()
            .append("path")
            .attr("class", "link")
            .attr("d", d3.linkHorizontal()
                .x(d => d.y)
                .y(d => d.x)
            );
        
        // Nodes
        const node = g.selectAll(".node")
            .data(root.descendants())
            .enter()
            .append("g")
            .attr("class", "node")
            .attr("transform", d => `translate(${d.y},${d.x})`);
        
        // Node circles
        node.append("circle")
            .attr("r", d => d.data.is_group ? 12 : 10)  // Larger circles for groups
            .attr("class", d => d.data.is_group ? "logic-group" : "logic-node")
            .on("click", function(event, d) {
                // On click, show node details
                alert(`Node: ${d.data.name}\\nType: ${d.data.is_group ? 'LogicGroup' : 'LogicNode'}\\nLabels: ${d.data.labels.join(', ')}`);
            });
        
        // Node labels
        node.append("text")
            .attr("dy", "0.35em")
            .attr("x", d => d.children || d._children ? -13 : 13)
            .attr("text-anchor", d => d.children || d._children ? "end" : "start")
            .attr("class", "node-label")
            .text(d => d.data.name);
        
        // Add group indication for LogicGroups
        node.filter(d => d.data.is_group)
            .append("text")
            .attr("dy", "-0.8em")
            .attr("x", 0)
            .attr("text-anchor", "middle")
            .attr("font-size", "8px")
            .attr("fill", "red")
            .text("G");  // 'G' for Group
        
        // Center the tree
        const centerX = (width - root.y) / 2;
        const centerY = (height - root.x) / 2;
        g.attr("transform", `translate(${centerX},${centerY})`);
        
        // Zoom functions
        function zoomIn() {
            d3.select("svg").transition().call(
                d3.zoom().scaleBy, 1.2
            );
        }
        
        function zoomOut() {
            d3.select("svg").transition().call(
                d3.zoom().scaleBy, 0.8
            );
        }
        
        function resetZoom() {
            d3.select("svg").transition().call(
                d3.zoom().transform,
                d3.zoomIdentity
            );
        }
    </script>
</body>
</html>
    """
    
    def convert_node_to_dict(node: LogicNode, visited=None) -> Dict[str, Any]:
        """Convert a LogicNode and its children to a dictionary structure for visualization."""
        if visited is None:
            visited = set()
            
        # Create unique ID for the node to prevent infinite recursion
        node_id = id(node)
        if node_id in visited:
            return {
                "name": f"{node.name} (duplicate)",
                "id": node_id,
                "is_group": isinstance(node, LogicGroup),
                "labels": getattr(node, 'labels', []),
                "children": []
            }
        
        visited.add(node_id)
        
        result = {
            "name": node.name,
            "id": node_id,
            "is_group": isinstance(node, LogicGroup),  # Check if it's a LogicGroup
            "labels": getattr(node, 'labels', []),  # Labels from LogicGroups
            "children": []
        }
        
        # Add children if they exist
        if hasattr(node, 'children'):
            for child_name, child_node in node.children.items():
                result["children"].append(convert_node_to_dict(child_node, visited.copy()))
        
        return result
    
    @app.route('/')
    def index():
        tree_dict = convert_node_to_dict(node)
        return render_template_string(html_template, tree_data=json.dumps(tree_dict))
    
    @app.route('/api/tree')
    def get_tree():
        tree_dict = convert_node_to_dict(node)
        return jsonify(tree_dict)
    
    return app


def show(node: LogicNode, host: str = "127.0.0.1", port: int = 5000, open_browser: bool = True, duration: int = 300):
    """
    Show the decision tree visualization in a web browser.
    
    Args:
        node: The LogicNode to visualize
        host: Host address for the Flask server
        port: Port for the Flask server
        open_browser: Whether to automatically open the browser
        duration: How long to keep the server running (in seconds)
    """
    app = create_app(node)
    
    def run_server():
        app.run(host=host, port=port, debug=False, use_reloader=False)
    
    # Start server in a separate thread
    server_thread = threading.Thread(target=run_server, daemon=True)
    server_thread.start()
    
    # Wait a bit for server to start
    time.sleep(1)
    
    # Open browser if requested
    if open_browser:
        webbrowser.open(f"http://{host}:{port}")
        print(f"Decision tree visualization opened at http://{host}:{port}")
        print(f"Server will run for {duration} seconds or until interrupted with Ctrl+C")
    
    # Keep the main thread alive for a while to allow viewing
    try:
        # Wait for a reasonable amount of time before closing
        time.sleep(duration)  # Default 5 minutes, but configurable
    except KeyboardInterrupt:
        print("\nVisualization closed by user.")
    finally:
        print("Shutting down the visualization server...")


if __name__ == "__main__":
    # Example usage - this would be replaced with actual node when called
    print("Web UI module for decision tree visualization")