# Decision Tree Web UI

This module provides a web-based visualization interface for decision trees with LogicGroup support.

## Features

- Interactive visualization of decision tree hierarchy
- Visual distinction between LogicNode and LogicGroup
- Support for nested structures
- Zoom and pan functionality
- Click interaction to view node details

## Dependencies

- Flask
- D3.js (loaded from CDN)

## Usage

```python
from decision_graph.decision_tree.webui.main import show
from decision_graph.decision_tree.capi.c_abc import LogicNode

# Create or load your decision tree
root_node = LogicNode("Root")

# Show the visualization
show(root_node, host="127.0.0.1", port=5000, open_browser=True, duration=300)
```

## Parameters

- `node`: The LogicNode to visualize
- `host`: Host address for the Flask server (default: "127.0.0.1")
- `port`: Port for the Flask server (default: 5000)
- `open_browser`: Whether to automatically open the browser (default: True)
- `duration`: How long to keep the server running in seconds (default: 300)

## Visual Indicators

- **Blue circles**: LogicNode instances
- **Red circles**: LogicGroup instances (larger size)
- **'G' label**: Indicates LogicGroup nodes
- **Node labels**: Show the name of each node

## Architecture

The visualization uses:
- Flask as the backend server
- D3.js for interactive tree visualization
- A recursive algorithm to convert the decision tree structure to JSON
- SVG for rendering the tree structure