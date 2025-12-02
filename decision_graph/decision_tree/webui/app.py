import json
import os
import pathlib
import queue
import socket
import threading
import time
from typing import Any

from . import LOGGER
from .. import LogicNode, ActionNode, BreakpointNode, TRUE_CONDITION, FALSE_CONDITION, ELSE_CONDITION, NO_CONDITION, RootLogicNode


class DecisionTreeWebUi(object):
    """Class to manage the Flask web UI for visualizing LogicNode trees."""
    _builtin_node_type = [
        'LogicNode', 'RootLogicNode', 'BreakpointNode',
        'ActionNode', 'NoAction', 'LongAction', 'ShortAction', 'CancelAction', 'ClearAction',
    ]

    def __init__(self, host: str, port: int, debug: bool):
        """
        Initializes the web UI manager.

        Args:
            host (str): The host address for the Flask server.
            port (int): The port for the Flask server.
            debug (bool): Whether to run Flask in debug mode.
        """
        from flask import Flask

        self.host = host
        self.port = port
        self.debug = debug
        self.app = Flask(__name__, template_folder='templates', static_folder='static')
        self.current_tree_data: dict[str, Any] | None = None
        self.current_tree_id: str | None = None
        self.node: LogicNode | None = None
        self.with_eval = False
        self.with_watch = False

        self._setup_routes()

    def _setup_routes(self):
        """Configures the Flask routes for the application."""
        from flask import render_template, jsonify

        @self.app.route('/')
        def index():
            if self.current_tree_data is None:
                return render_template(
                    'index.html',
                    initial_tree_data={},
                    tree_id="empty",
                    with_eval=self.with_eval,
                    with_watch=False
                )
            else:
                return render_template(
                    'index.html',
                    initial_tree_data=self.current_tree_data,
                    tree_id=self.current_tree_id,
                    with_eval=self.with_eval,
                    with_watch=self.with_watch
                )

        @self.app.route('/api/tree_data')
        def get_tree_data():
            if self.current_tree_data is None:
                return jsonify({"error": "No tree data available"}), 404
            return jsonify({"tree_data": self.current_tree_data, "tree_id": self.current_tree_id})

        @self.app.route('/api/active_nodes')
        def get_active_nodes():
            # Returns the current set of active node IDs as JSON
            if self.node is not None:
                try:
                    if isinstance(self.node, RootLogicNode):
                        active_ids = [str(n.uid) for n in self.node.eval_path]
                    else:
                        active_ids = [str(n.uid) for n in self.node.eval_recursively()[1]]
                    return jsonify({'active_ids': active_ids})
                except Exception:
                    LOGGER.error("Error getting active nodes", exc_info=True)
            return jsonify({'active_ids': []})

    @classmethod
    def _convert_node_to_dict(
            cls,
            node: LogicNode,
            visited_nodes: dict[str, dict[str, Any]],
            virtual_parent_links: list[dict[str, Any]],
            activated_node_ids: set = None
    ) -> dict[str, Any]:
        """Recursively converts a LogicNode tree into a dictionary format suitable for JSON/D3."""
        node_id = str(node.uid)
        if node_id in visited_nodes:
            return {"id": node_id, "is_reference": True}

        node_type = node.__class__.__name__
        if node_type not in cls._builtin_node_type:
            if isinstance(node, ActionNode):
                node_type = "ActionNode"
            else:
                node_type = "LogicNode"

        # Determine if node is activated (only if activated_node_ids is provided)
        is_activated = activated_node_ids is None or node_id in activated_node_ids

        node_obj: dict[str, Any] = {
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
        if node_type == 'BreakpointNode':
            # For BreakpointNode, should not scan its only child, but just add to virtual_parent_links
            node: BreakpointNode
            child_node = node.linked_to
            if child_node is not None:
                condition_type = "unconditional"
                if child_node.parent is node:
                    child_dict = cls._convert_node_to_dict(child_node, visited_nodes, virtual_parent_links, activated_node_ids)
                    child_with_condition = child_dict.copy()
                    child_with_condition['condition_to_child'] = "unconditional"
                    child_with_condition['condition_type'] = condition_type
                    node_obj["_children"].append(child_with_condition)
                else:
                    virtual_parent_links.append(
                        {
                            "source": node_id,
                            "target": str(child_node.uid),
                            "type": "virtual_parent"
                        }
                    )
        else:
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

                child_dict = cls._convert_node_to_dict(child_node, visited_nodes, virtual_parent_links, activated_node_ids)
                child_with_condition = child_dict.copy()
                child_with_condition['condition_to_child'] = f'<{str(condition)}>' if condition is not None else "<Unknown>"
                child_with_condition['condition_type'] = condition_type
                node_obj["_children"].append(child_with_condition)
        return node_obj

    @classmethod
    def _convert_tree_to_d3_format(cls, root_node: LogicNode, activated_node_ids: set = None) -> dict[str, Any]:
        """Converts the LogicNode tree into a D3 hierarchical format."""
        visited_nodes = {}
        virtual_parent_links = []

        root_dict = cls._convert_node_to_dict(root_node, visited_nodes, virtual_parent_links, activated_node_ids)

        return {
            "root": root_dict,
            "virtual_links": virtual_parent_links
        }

    def _port_is_free(self, port: int) -> bool:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                s.bind((self.host, port))
            return True
        except OSError:
            return False

    def _auto_port(self, max_tries: int = 200) -> int:
        requested_port = self.port
        if not self._port_is_free(requested_port):
            LOGGER.warning(f"Port {requested_port} is in use — searching for a free port...")
            # Try next N ports, then fall back to ephemeral port
            for i in range(max_tries):
                requested_port += 1
                if self._port_is_free(requested_port):
                    return requested_port
            # If no port found in range, get an ephemeral port
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                s.bind((self.host, 0))
                requested_port = s.getsockname()[1]
            LOGGER.info(f"Selected alternative port {requested_port} for this session")
        return requested_port

    @staticmethod
    def _open_browser(url):
        import webbrowser
        def open_browser():
            time.sleep(1)
            try:
                webbrowser.open(url)
            except Exception:
                LOGGER.exception("Failed to open browser for URL: %s", url)

        browser_thread = threading.Thread(target=open_browser)
        browser_thread.daemon = True
        browser_thread.start()

    def show(self, node: LogicNode, with_eval: bool = True):
        """Starts the Flask web UI to visualize a LogicNode tree."""
        if not isinstance(node, LogicNode):
            raise TypeError("The 'node' argument must be an instance of LogicNode or its subclass.")

        LOGGER.info(f"Preparing to visualize LogicNode tree starting at {node}")

        activated_node_ids = None if not with_eval \
            else {str(n.uid) for n in node.eval_path} if isinstance(node, RootLogicNode) \
            else {str(n.uid) for n in node.eval_recursively()[1]}
        self.with_eval = with_eval
        self.current_tree_data = self._convert_tree_to_d3_format(node, activated_node_ids)
        self.current_tree_id = str(node.uid)
        self.with_watch = False

        port_to_use = self._auto_port()
        url = f"http://{self.host}:{port_to_use}"
        self._open_browser(url)
        LOGGER.info(f"Starting Flask server on {url} (with_eval={with_eval})")
        self.app.run(host=self.host, port=port_to_use, debug=self.debug, use_reloader=False, threaded=True)

    def watch(self, node: RootLogicNode, interval: float = 0.5, block: bool = False):
        """
        Starts a watch server that streams activation diffs via SSE.
        Each client connection gets its own worker and queue, so multiple tabs/windows do not interfere.
        If block is False, runs Flask in a background thread and returns immediately.
        """
        from flask import Response, stream_with_context
        last_activated = set(str(n.uid) for n in node.eval_path)
        self.current_tree_data = self._convert_tree_to_d3_format(node, last_activated)
        self.current_tree_id = str(node.uid)
        self.with_eval = True
        self.with_watch = True
        self.node = node

        @self.app.route('/watch')
        def sse_watch():
            q = queue.Queue()
            stop_event = threading.Event()

            def worker():
                nonlocal last_activated
                while not stop_event.is_set():
                    activated_now = set(str(n.uid) for n in node.eval_path)
                    added = list(activated_now - last_activated)
                    removed = list(last_activated - activated_now)
                    if added or removed:
                        q.put(json.dumps({'added': added, 'removed': removed}))
                        last_activated = activated_now
                    time.sleep(interval)

            t = threading.Thread(target=worker)
            t.daemon = True
            t.start()

            def event_stream():
                try:
                    while True:
                        try:
                            diff = q.get(timeout=interval)
                        except queue.Empty:
                            continue
                        LOGGER.info(f"Watching {diff}")
                        yield f"data: {diff}\n\n"
                except GeneratorExit:
                    stop_event.set()

            return Response(stream_with_context(event_stream()), mimetype='text/event-stream')

        port_to_use = self._auto_port()
        url = f"http://{self.host}:{port_to_use}"
        self._open_browser(url)
        LOGGER.info(f"Starting SSE watch server on port {port_to_use}")
        if block:
            self.app.run(host=self.host, port=port_to_use, debug=self.debug, use_reloader=False, threaded=True)
        else:
            def run_flask():
                self.app.run(host=self.host, port=port_to_use, debug=self.debug, use_reloader=False, threaded=True)

            flask_thread = threading.Thread(target=run_flask)
            flask_thread.daemon = True
            flask_thread.start()
            return flask_thread

    @classmethod
    def to_html(cls, node: LogicNode, file_name: str, with_eval: bool = True):
        """
        Exports a LogicNode tree as a self-contained offline HTML file.

        Args:
            node (LogicNode): The root node of the tree to visualize.
            file_name (str): Output HTML file path.
            with_eval (bool): Whether to include evaluation (activation) data.
        """
        from jinja2 import Environment, FileSystemLoader

        if not isinstance(node, LogicNode):
            raise TypeError("The 'node' argument must be an instance of LogicNode or its subclass.")

        if with_eval:
            try:
                if isinstance(node, RootLogicNode) and node.eval_path:
                    activated_node_ids = {str(n.uid) for n in node.eval_path}
                else:
                    activated_node_ids = {str(n.uid) for n in node.eval_recursively()[1]}
            except Exception:
                LOGGER.error(f"Could not find evaluation path for node {node}", exc_info=True)
                activated_node_ids = None
        else:
            activated_node_ids = None

        tree_data = cls._convert_tree_to_d3_format(node, activated_node_ids)

        # Locate resource directories
        module_dir = pathlib.Path(__file__).parent
        template_dir = module_dir / "templates"
        static_dir = module_dir / "static"
        css_path = static_dir / "style.css"
        js_path = static_dir / "script.js"
        d3_path = static_dir / "d3.v7.min.js"

        with open(css_path, "r", encoding="utf-8") as f:
            css_content = f.read()

        with open(js_path, "r", encoding="utf-8") as f:
            js_content = f.read()

        with open(d3_path, "r", encoding="utf-8") as f:
            d3_content = f.read()

        env = Environment(loader=FileSystemLoader(template_dir))
        template = env.get_template("offline.html")
        html_output = template.render(
            initial_tree_data=tree_data,
            with_eval=with_eval,
            css_content=css_content,
            js_content=js_content,
            d3_content=d3_content
        )

        with open(file_name, "w", encoding="utf-8") as f:
            f.write(html_output)

        LOGGER.info(f'Offline HTML exported to: "{os.path.realpath(file_name)}"')
