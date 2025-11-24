import threading
import time
import webbrowser
from pathlib import Path
from typing import Dict, List, Any, Optional
from uuid import uuid4

from flask import Flask, render_template, jsonify
from jinja2 import Environment, FileSystemLoader

from decision_graph.decision_tree import LOGGER
from decision_graph.decision_tree.capi import LogicNode, ActionNode, BreakpointNode, NoAction, LongAction, ShortAction, TRUE_CONDITION, FALSE_CONDITION, ELSE_CONDITION, NO_CONDITION

LOGGER = LOGGER.getChild('WebUI')


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
        self._with_eval = False

    def _setup_routes(self):
        """Configures the Flask routes for the application."""

        @self.app.route('/')
        def index():
            if self.current_tree_data is None:
                # If no tree is loaded, render with empty data or an error message
                return render_template('index.html', initial_tree_data={}, tree_id="empty", with_eval=self._with_eval)
            return render_template('index.html', initial_tree_data=self.current_tree_data, tree_id=self.current_tree_id, with_eval=self._with_eval)

        @self.app.route('/api/tree_data')
        def get_tree_data():
            if self.current_tree_data is None:
                return jsonify({"error": "No tree data available"}), 404
            return jsonify({"tree_data": self.current_tree_data, "tree_id": self.current_tree_id})

    def _convert_node_to_dict(
            self,
            node: LogicNode,
            visited_nodes: Dict[int, Dict[str, Any]],
            virtual_parent_links: List[Dict[str, Any]],
            activated_node_ids: Optional[set] = None
    ) -> Dict[str, Any]:
        """Recursively converts a LogicNode tree into a dictionary format suitable for JSON/D3."""
        node_id = id(node)
        if node_id in visited_nodes:
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

        # Determine if node is activated (only if activated_node_ids is provided)
        is_activated = activated_node_ids is None or node_id in activated_node_ids

        node_obj = {
            "id": node_id,
            "name": node.repr,
            "repr": repr(node),
            "type": node_type,
            "labels": node.labels,
            "autogen": node.autogen,
            "_children": [],
            "activated": is_activated
        }

        visited_nodes[node_id] = node_obj

        # Process children
        for condition, child_node in node.children.items():
            if condition == TRUE_CONDITION:
                condition_type = "true"
            elif condition == FALSE_CONDITION:
                condition_type = "false"
            elif condition == ELSE_CONDITION:
                condition_type = "else"
            elif condition == NO_CONDITION:
                condition_type = "unconditional"
            else:
                condition_type = "other"

            child_dict = self._convert_node_to_dict(child_node, visited_nodes, virtual_parent_links, activated_node_ids)
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

    def convert_tree_to_d3_format(self, root_node: LogicNode, activated_node_ids: Optional[set] = None) -> Dict[str, Any]:
        """Converts the LogicNode tree into a D3 hierarchical format."""
        visited_nodes = {}
        virtual_parent_links = []

        root_dict = self._convert_node_to_dict(root_node, visited_nodes, virtual_parent_links, activated_node_ids)

        # Build virtual links (unchanged logic)
        all_node_ids = set(visited_nodes.keys())
        for node_id in all_node_ids:
            node_obj = visited_nodes[node_id]
            if node_obj["type"] == "BreakpointNode":
                for target_id in all_node_ids:
                    if target_id != node_id:
                        virtual_parent_links.append(
                            {
                                "source": node_id,
                                "target": target_id,
                                "type": "virtual_parent"
                            }
                        )

        return {
            "root": root_dict,
            "virtual_links": virtual_parent_links
        }

    def show(self, node: LogicNode, with_eval: bool = True, **kwargs):
        """Starts the Flask web UI to visualize a LogicNode tree."""
        if not isinstance(node, LogicNode):
            raise TypeError("The 'node' argument must be an instance of LogicNode or its subclass.")

        LOGGER.info(f"Preparing to visualize LogicNode tree starting at {node}")

        activated_node_ids = None
        if with_eval:
            v, p = node.eval_recursively()
            activated_node_ids = {id(n) for n in p}

        self.current_tree_data = self.convert_tree_to_d3_format(node, activated_node_ids)
        self.current_tree_id = str(uuid4())

        url = f"http://{self.host}:{self.port}"

        def open_browser():
            time.sleep(1)
            webbrowser.open(url)

        browser_thread = threading.Thread(target=open_browser)
        browser_thread.start()

        LOGGER.info(f"Starting Flask server on {url} (with_eval={with_eval})")
        try:
            # Pass with_eval to route context
            self._with_eval = with_eval  # Store temporarily
            self.app.run(host=self.host, port=self.port, debug=self.debug, use_reloader=False, threaded=True)
        except KeyboardInterrupt:
            LOGGER.info("Flask server stopped by user.")
        finally:
            LOGGER.info("Web UI session ended.")

    def to_html(self, node: LogicNode, file_name: str, with_eval: bool = True):
        """
        Exports a LogicNode tree as a self-contained offline HTML file.

        Args:
            node (LogicNode): The root node of the tree to visualize.
            file_name (str): Output HTML file path.
            with_eval (bool): Whether to include evaluation (activation) data.
        """
        if not isinstance(node, LogicNode):
            raise TypeError("The 'node' argument must be an instance of LogicNode or its subclass.")

        # Prepare tree data
        activated_node_ids = None
        if with_eval:
            v, p = node.eval_recursively()
            activated_node_ids = {id(n) for n in p}

        tree_data = self.convert_tree_to_d3_format(node, activated_node_ids)

        # Determine if evaluation data is present (for toggle visibility)
        def has_inactive(node_dict):
            if node_dict.get("activated") is False:
                return True
            return any(has_inactive(child) for child in node_dict.get("_children", []))

        has_eval_data = with_eval and has_inactive(tree_data["root"])

        # Locate resource directories
        module_dir = Path(__file__).parent
        template_dir = module_dir / "templates"
        static_dir = module_dir / "static"

        # Read assets
        css_path = static_dir / "style.css"
        js_path = static_dir / "script.js"
        template_path = template_dir / "offline.html"

        for p in [css_path, js_path, template_path]:
            if not p.exists():
                raise FileNotFoundError(f"Required resource not found: {p}")

        with open(css_path, "r", encoding="utf-8") as f:
            css_content = f.read()
        with open(js_path, "r", encoding="utf-8") as f:
            js_content = f.read()

        # Render template
        env = Environment(loader=FileSystemLoader(template_dir))
        template = env.get_template("offline.html")
        html_output = template.render(
            initial_tree_data=tree_data,
            with_eval=has_eval_data,
            css_content=css_content,
            js_content=js_content
        )

        # Write file
        with open(file_name, "w", encoding="utf-8") as f:
            f.write(html_output)

        LOGGER.info(f"Offline HTML exported to: {file_name}")
