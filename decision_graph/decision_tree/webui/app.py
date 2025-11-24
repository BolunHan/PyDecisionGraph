# decision_graph/decision_tree/webui/main.py

import argparse
import webbrowser
import threading
import time
import logging
from typing import Dict, List, Any, Optional
from uuid import uuid4
import flask
from flask import Flask, render_template, jsonify, request
from decision_graph.decision_tree.capi import LogicNode, LogicGroup, ActionNode, BreakpointNode, NoAction, LongAction, ShortAction, NodeEdgeCondition, TRUE_CONDITION, FALSE_CONDITION, ELSE_CONDITION, NO_CONDITION

# --- Configuration ---
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 5000
FLASK_DEBUG = False  # Set to True for development debugging
LOG_LEVEL = logging.INFO  # Or logging.DEBUG for more detail
# --- End Configuration ---

logging.basicConfig(level=LOG_LEVEL)
logger = logging.getLogger(__name__)


class DecisionTreeWebUi(object):
    """Class to manage the Flask web UI for visualizing LogicNode trees."""

    def __init__(self, host: str, port: int, debug: bool):
        """
        Initializes the web UI manager.

        Args:
            host (str): The host address for the Flask server.
            port (int): The port for the Flask server.
            debug (bool): Whether to run Flask in debug mode.
        """
        self.host = host
        self.port = port
        self.debug = debug
        self.app = Flask(__name__, template_folder='templates', static_folder='static')
        self.current_tree_data: Optional[Dict[str, Any]] = None
        self.current_tree_id: Optional[str] = None
        self._setup_routes()

    def _setup_routes(self):
        """Configures the Flask routes for the application."""

        @self.app.route('/')
        def index():
            if self.current_tree_data is None:
                # If no tree is loaded, render with empty data or an error message
                return render_template('index.html', initial_tree_data={}, tree_id="empty")
            return render_template('index.html', initial_tree_data=self.current_tree_data, tree_id=self.current_tree_id)

        @self.app.route('/api/tree_data')
        def get_tree_data():
            if self.current_tree_data is None:
                return jsonify({"error": "No tree data available"}), 404
            return jsonify({"tree_data": self.current_tree_data, "tree_id": self.current_tree_id})

    def _convert_node_to_dict(
            self,
            node: LogicNode,
            visited_nodes: Dict[int, Dict[str, Any]],
            virtual_parent_links: List[Dict[str, Any]]
    ) -> Dict[str, Any]:
        """Recursively converts a LogicNode tree into a dictionary format suitable for JSON/D3."""
        node_id = id(node)  # Use Python's object ID as a unique identifier for the node

        if node_id in visited_nodes:
            # If already visited, return the existing reference object
            # This handles cycles or shared sub-trees if they exist in the structure
            return {"id": node_id, "is_reference": True}

        # Determine node type for styling
        if isinstance(node, BreakpointNode):
            node_type = "BreakpointNode"
        elif isinstance(node, ActionNode):
            if isinstance(node, NoAction):
                node_type = "NoAction"
            elif isinstance(node, LongAction):
                node_type = "LongAction"
            elif isinstance(node, ShortAction):
                node_type = "ShortAction"
            else:
                node_type = "ActionNode"
        else:
            node_type = "LogicNode"

        # Create the node object
        node_obj = {
            "id": node_id,
            "name": node.repr,  # Use repr or ID as name
            "type": node_type,
            "labels": node.labels,
            "autogen": node.autogen,
            "_children": []  # Will be populated later
        }

        # Store the object reference to prevent infinite recursion and to link children
        visited_nodes[node_id] = node_obj

        # Process children
        for condition, child_node in node.children.items():
            # Determine condition type string for styling
            if condition == TRUE_CONDITION:
                condition_type = "true"
            elif condition == FALSE_CONDITION:
                condition_type = "false"
            elif condition == ELSE_CONDITION:
                condition_type = "else"
            elif condition == NO_CONDITION:  # Unconditional
                condition_type = "unconditional"
            else:
                condition_type = "other"
            child_dict = self._convert_node_to_dict(child_node, visited_nodes, virtual_parent_links)
            child_with_condition = child_dict.copy()
            child_with_condition['condition_to_child'] = f'<{str(condition)}>' if condition is not None else "<Unknown>"
            child_with_condition['condition_type'] = condition_type
            node_obj["_children"].append(child_with_condition)

        return node_obj

    def _find_virtual_parents(self, root_node: LogicNode, target_node_id: int) -> List[LogicNode]:
        """Finds all BreakpointNodes in the tree that target the given node_id."""
        # Note: This is a simplified placeholder based on the limitations discussed previously.
        # A robust implementation requires specific details from the core library.
        virtual_parents = []

        def scan(node):
            if isinstance(node, BreakpointNode):
                virtual_parents.append(node)
            for child_node in node.children.values():
                scan(child_node)

        scan(root_node)
        return [bp for bp in virtual_parents if id(bp) != target_node_id]

    def convert_tree_to_d3_format(self, root_node: LogicNode) -> Dict[str, Any]:
        """Converts the LogicNode tree into a D3 hierarchical format."""
        visited_nodes = {}
        virtual_parent_links = []

        # Convert the main tree structure
        root_dict = self._convert_node_to_dict(root_node, visited_nodes, virtual_parent_links)

        # Now, find virtual parent links (BreakpointNodes pointing to other nodes)
        all_node_ids = set(visited_nodes.keys())
        for node_id in all_node_ids:
            node_obj = visited_nodes[node_id]
            if node_obj["type"] == "BreakpointNode":
                # A BreakpointNode might target another node in the tree.
                # We'll add a link from the BP node to *all* other nodes as a potential virtual link.
                # This is a simplification. The core library likely has specific logic.
                # The frontend should be aware this is a potential link, not a direct parent-child.
                for target_id in all_node_ids:
                    if target_id != node_id:  # Avoid self-link
                        virtual_parent_links.append({
                            "source": node_id,
                            "target": target_id,
                            "type": "virtual_parent"  # Distinguish from regular parent-child
                        })

        return {
            "root": root_dict,
            "virtual_links": virtual_parent_links
        }

    def show(self, node: LogicNode, **kwargs):
        """
        Starts the Flask web UI to visualize a LogicNode tree.

        Args:
            node (LogicNode): The root node of the tree to visualize.
            **kwargs: Additional arguments passed during initialization (host, port, debug).
        """
        if not isinstance(node, LogicNode):
            raise TypeError("The 'node' argument must be an instance of LogicNode or its subclass.")

        logger.info(f"Preparing to visualize LogicNode tree starting at {node}")
        self.current_tree_data = self.convert_tree_to_d3_format(node)
        self.current_tree_id = str(uuid4())  # Generate a unique ID for this visualization session

        # Determine URL and prepare to open browser
        url = f"http://{self.host}:{self.port}"

        def open_browser():
            time.sleep(1)  # Wait a bit for the server to start
            webbrowser.open(url)

        browser_thread = threading.Thread(target=open_browser)
        browser_thread.start()

        logger.info(f"Starting Flask server on {url}")
        try:
            self.app.run(host=self.host, port=self.port, debug=self.debug, use_reloader=False, threaded=True)
        except KeyboardInterrupt:
            logger.info("Flask server stopped by user.")
        finally:
            logger.info("Web UI session ended.")
