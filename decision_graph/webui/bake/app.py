"""The bake layer's viewer: a baked graph, drawn as cards.

What this reads is the bake layer's own surface and nothing else — the node
types the C layer reports (``ROOT``, ``BINARY``, ``LONGACTION``, …), the store
labels a build hung on its nodes, and the evaluation record a walk filled in.

Two things make a bake graph different to draw, and both are here rather than
in the front end:

- a node's OPERANDS are not its children. A ``BINARY`` node's operands are
  nodes of their own, reachable in C but not through ``children``, so a drawing
  that followed the children alone would show the branches and hide the
  arithmetic. The node's display text carries them (``probe.signal > 0``), and
  that is what the card is given.
- a BREAKPOINT resumes into a node that already sits under another parent. It
  is drawn with a "virtual parent" link rather than a second child edge, so the
  continuation is drawn once and the break still says where the build stopped.
"""

import json
import os
import pathlib
import queue
import socket
import threading
import time
from typing import Any

from . import LOGGER
# Scoped on purpose: this UI draws what the bake layer built, and reaches it by
# name rather than through the package that re-exports whichever layer won.
from decision_graph.decision_tree.bake.c_hierarchy import BreakpointNode, RootLogicNode
from decision_graph.decision_tree.bake.c_node import LogicNode


class BakeWebUi(object):
    """Class to manage the Flask web UI for visualizing a baked logic graph."""

    #: The type families the C layer reports, keyed by the family nibble of the
    #: type name. The front end colours a card by this rather than by its type,
    #: so a type this file has never heard of still draws as its family.
    _family_of = {
        'TRUE': 'INPUT', 'FALSE': 'INPUT', 'DOUBLE': 'INPUT', 'STRING': 'INPUT',
        'INT': 'INPUT', 'VARIABLE': 'INPUT',
        'UNARY': 'OP', 'BINARY': 'OP', 'TERNARY': 'OP', 'CALL': 'OP',
        'NOACTION': 'ACTION', 'LONGACTION': 'ACTION', 'SHORTACTION': 'ACTION',
        'CANCELACTION': 'ACTION', 'CLEARACTION': 'ACTION', 'PLACEHOLDER': 'ACTION',
        'ROOT': 'SPECIAL', 'BREAKPOINT': 'SPECIAL',
    }

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
        from flask import render_template, jsonify, request

        @self.app.route('/')
        def index():
            return render_template(
                'index.html',
                initial_tree_data=self.current_tree_data or {},
                tree_id=self.current_tree_id or "empty",
                with_eval=self.with_eval,
                with_watch=self.with_watch,
            )

        @self.app.route('/api/tree_data')
        def get_tree_data():
            if self.current_tree_data is None:
                return jsonify({"error": "No tree data available"}), 404
            return jsonify({"tree_data": self.current_tree_data, "tree_id": self.current_tree_id})

        @self.app.route('/api/active_nodes')
        def get_active_nodes():
            return jsonify(self._activation())

        @self.app.route('/api/evaluate', methods=['POST'])
        def evaluate():
            """Walk the graph now, and answer with what the walk landed on.

            The page asks for this rather than being handed an evaluation, so
            that opening a viewer never runs a graph on its own: a walk is a
            side effect on the node's own storage, and the caller decides when
            to spend it.
            """
            if not isinstance(self.node, RootLogicNode):
                return jsonify({'error': 'The viewer is not holding a root node.'}), 400
            try:
                self.node.eval()
            except Exception as error:
                LOGGER.warning("Evaluation failed: %s", error)
                return jsonify({**self._activation(), 'error': str(error)})
            return jsonify(self._activation())

    def _activation(self) -> dict[str, Any]:
        """The nodes of the last walk, by id, with the outcome it recorded."""
        walk = self._walk_record(self.node)
        if walk is None:
            return {'active_ids': []}
        return {'active_ids': walk['ids'], 'code': walk['code'], 'leaf': walk['leaf'],
                'failed': walk['failed'], 'length': walk['length']}

    @classmethod
    def _walk_record(cls, node: LogicNode) -> dict[str, Any] | None:
        """The walk a root has already recorded, or None when it has none.

        Read, never taken: a viewer that evaluated on its own would run a graph
        the caller only asked to look at. An empty record means nobody has
        walked it yet, which the page shows as "not walked" rather than as an
        empty path.
        """
        if not isinstance(node, RootLogicNode):
            return None
        try:
            path = node.eval_path
            if not len(path):
                return None
            return {
                'ids': [str(n.uuid) for n in path],
                'code': path.code_name,
                'leaf': None if path.leaf is None else str(path.leaf.uuid),
                'failed': None if path.failed is None else str(path.failed.uuid),
                'length': len(path),
            }
        except Exception:
            LOGGER.error("Error reading the evaluation record", exc_info=True)
            return None

    @classmethod
    def _var_to_text(cls, node: LogicNode) -> str | None:
        """The value a node holds in its own slot, as text, or None.

        Read defensively: a slot is the node's workspace, and a node that has
        never been evaluated holds whatever it was born with - which for most
        types is the null a card says nothing about.
        """
        try:
            view = node.out
            if view.is_null:
                return None
            value = view.value
        except Exception:
            return None
        if value is None:
            return None
        return repr(value) if isinstance(value, str) else str(value)

    @classmethod
    def _own_value(cls, node: LogicNode) -> str | None:
        """The payload a leaf carries itself: a literal's value, a read's entry.

        Asked of the layer rather than inferred from the display text, and
        guarded: only the input family answers it, and a node that is not one
        refuses the question.
        """
        try:
            value = node.value
        except Exception:
            return None
        if value is None:
            return None
        return repr(value) if isinstance(value, str) else str(value)

    @classmethod
    def _operands_of(cls, node: LogicNode) -> list[dict[str, Any]]:
        """The nodes an expression was built over.

        An operand is NOT a child: it is reachable in C through the node's own
        argument array, and a walk over ``children`` alone goes straight past it.
        This is what the card needs to say what the arithmetic is over, rather
        than leaving it to be read out of the display text.
        """
        try:
            operands = node.operands
        except Exception:
            return []
        return [
            {"id": str(operand.uuid), "type": operand.type, "repr": operand.repr}
            for operand in operands if operand is not None
        ]

    @classmethod
    def _condition_to_dict(cls, condition: Any) -> dict[str, Any]:
        """An edge's condition, in the four shapes the front end draws.

        ``value`` is asked for last and guarded: a condition that carries no
        payload refuses the question, and a build's TRUE/FALSE arm answers it
        with the bool the arm is. The refusal is the ordinary case here, not an
        error.
        """
        try:
            value = condition.value
        except Exception:
            value = None

        return {
            'text': str(condition),
            'is_binary': bool(condition.is_binary),
            'is_else': bool(condition.is_else),
            'is_none': bool(condition.is_none),
            'value': value if isinstance(value, (bool, int, float, str)) else None,
        }

    @classmethod
    def _convert_node_to_dict(
            cls,
            node: LogicNode,
            visited_nodes: dict[str, dict[str, Any]],
            virtual_parent_links: list[dict[str, Any]],
            activated_node_ids: set | None = None,
    ) -> dict[str, Any]:
        """Recursively converts a bake graph into the card model the page draws."""
        node_id = str(node.uuid)
        if node_id in visited_nodes:
            # The same block reached twice: a resumed node, or a join. The card
            # is drawn once, where it was first reached.
            return {"id": node_id, "is_reference": True}

        node_type = node.type
        node_obj: dict[str, Any] = {
            "id": node_id,
            "type": node_type,
            "family": cls._family_of.get(node_type, 'OTHER'),
            "repr": node.repr,
            "labels": list(node.labels),
            "autogen": bool(node.autogen),
            "is_leaf": bool(node.is_leaf),
            "size": int(node.size),
            "address": int(node.address),
            "out": cls._var_to_text(node),
            "self_value": cls._own_value(node),
            "operands": cls._operands_of(node),
            "hooks": list(node.eval_hooks),
            "_children": [],
            "activated": activated_node_ids is None or node_id in activated_node_ids,
        }

        visited_nodes[node_id] = node_obj

        if node_type == 'BREAKPOINT':
            # A breakpoint's only child is the node it resumed into - and that
            # node is already drawn where the branch was carried on from, so it
            # becomes a virtual link rather than a child edge. Drawing it as a
            # child would draw the same subtree twice and say the branch forks
            # here, which is the one thing a break did not do.
            node: BreakpointNode
            child_node = node.linked_to
            node_obj["await_connection"] = bool(node.await_connection)
            if child_node is not None:
                if child_node.parent is node:
                    child_dict = cls._convert_node_to_dict(child_node, visited_nodes, virtual_parent_links, activated_node_ids)
                    node_obj["_children"].append({
                        # The arm the break left on: the condition the
                        # continuation hangs by, read off the continuation.
                        "condition": cls._condition_to_dict(child_node.condition_to_parent),
                        "node": child_dict,
                    })
                else:
                    virtual_parent_links.append({
                        "source": node_id,
                        "target": str(child_node.uuid),
                        "type": "virtual_parent",
                    })
            return node_obj

        for condition, child_node in node.children.items():
            child_dict = cls._convert_node_to_dict(child_node, visited_nodes, virtual_parent_links, activated_node_ids)
            node_obj["_children"].append({
                "condition": cls._condition_to_dict(condition),
                "node": child_dict,
            })
        return node_obj

    @classmethod
    def _convert_tree_to_format(cls, root_node: LogicNode, walk: dict[str, Any] | None = None) -> dict[str, Any]:
        """Converts a bake graph into the payload the page draws."""
        visited_nodes: dict[str, dict[str, Any]] = {}
        virtual_parent_links: list[dict[str, Any]] = []

        activated_node_ids = None if walk is None else set(walk['ids'])
        root_dict = cls._convert_node_to_dict(root_node, visited_nodes, virtual_parent_links, activated_node_ids)

        # What the graph still carries of the BUILD: a breakpoint a split build
        # stopped at, and a placeholder a branch reserved and never filled. Both
        # are gone once the bake has run, so counting them is what tells the two
        # states of a graph apart without having to have seen the report.
        breakpoints = sum(1 for n in visited_nodes.values() if n['type'] == 'BREAKPOINT')
        placeholders = sum(1 for n in visited_nodes.values() if n['type'] == 'PLACEHOLDER')

        return {
            "root": root_dict,
            "virtual_links": virtual_parent_links,
            "n_nodes": len(visited_nodes),
            "root_name": getattr(root_node, 'name', None) or root_node.repr,
            "scaffolding": {"breakpoints": breakpoints, "placeholders": placeholders},
            "active_ids": [] if walk is None else walk['ids'],
            "walk": walk,
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
            for i in range(max_tries):
                requested_port += 1
                if self._port_is_free(requested_port):
                    return requested_port
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
        """Starts the Flask web UI to visualize a bake graph."""
        if not isinstance(node, LogicNode):
            raise TypeError("The 'node' argument must be an instance of LogicNode or its subclass.")

        LOGGER.info(f"Preparing to visualize bake graph starting at {node}")

        self.with_eval = with_eval
        self.current_tree_data = self._convert_tree_to_format(node, self._walk_record(node) if with_eval else None)
        self.current_tree_id = str(node.uuid)
        self.node = node
        self.with_watch = False

        port_to_use = self._auto_port()
        url = f"http://{self.host}:{port_to_use}"
        self._open_browser(url)
        LOGGER.info(f"Starting Flask server on {url} (with_eval={with_eval})")
        self.app.run(host=self.host, port=port_to_use, debug=self.debug, use_reloader=False, threaded=True)

    def watch(self, node: RootLogicNode, interval: float = 0.5, block: bool = False):
        """
        Starts a watch server that streams activation diffs via SSE.

        A baked graph is immutable, but the STORE it reads is not: the caller
        refills an entry and walks the graph again, and what moves is which
        nodes the walk landed on. Each client connection gets its own worker and
        queue, so multiple tabs do not interfere.
        """
        from flask import Response, stream_with_context

        walk = self._walk_record(node)
        last_activated = set() if walk is None else set(walk['ids'])
        self.current_tree_data = self._convert_tree_to_format(node, walk)
        self.current_tree_id = str(node.uuid)
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
                    activated_now = {str(n.uuid) for n in node.eval_path}
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
        Exports a bake graph as a self-contained offline HTML file.

        The page is the one the server would have served, with the stylesheet,
        the script and D3 read off disk and written into it — so the file opens
        somewhere with no server, no network and no package installed.

        Args:
            node (LogicNode): The root node of the graph to visualize.
            file_name (str): Output HTML file path.
            with_eval (bool): Whether to carry the evaluation record into the page.
        """
        from jinja2 import Environment, FileSystemLoader

        if not isinstance(node, LogicNode):
            raise TypeError("The 'node' argument must be an instance of LogicNode or its subclass.")

        tree_data = cls._convert_tree_to_format(node, cls._walk_record(node) if with_eval else None)

        module_dir = pathlib.Path(__file__).parent
        template_dir = module_dir / "templates"
        static_dir = module_dir / "static"

        def read(name: str) -> str:
            with open(static_dir / name, "r", encoding="utf-8") as f:
                return f.read()

        env = Environment(loader=FileSystemLoader(template_dir))
        template = env.get_template("offline.html")
        html_output = template.render(
            initial_tree_data=tree_data,
            with_eval=with_eval,
            css_content=read("style.css"),
            js_content=read("script.js"),
            d3_content=read("d3.v7.min.js"),
        )

        with open(file_name, "w", encoding="utf-8") as f:
            f.write(html_output)

        LOGGER.info(f'Offline HTML exported to: "{os.path.realpath(file_name)}"')
