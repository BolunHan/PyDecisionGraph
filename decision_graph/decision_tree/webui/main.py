# decision_graph/decision_tree/webui/main.py

import argparse
import logging

# --- Configuration ---
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 5000
FLASK_DEBUG = False  # Set to True for development debugging
LOG_LEVEL = logging.INFO  # Or logging.DEBUG for more detail
# --- End Configuration ---

logging.basicConfig(level=LOG_LEVEL)
logger = logging.getLogger(__name__)


def parse() -> argparse.Namespace:
    """Parses command-line arguments."""
    parser = argparse.ArgumentParser(description="Visualize a Decision Tree.")
    parser.add_argument('--host', type=str, default=DEFAULT_HOST, help='Host address for the web server (default: %(default)s)')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT, help='Port for the web server (default: %(default)s)')
    parser.add_argument('--debug', action='store_true', help='Enable Flask debug mode')
    # Note: The actual LogicNode object cannot be passed via command line.
    # This parser is for server configuration. The `show` function needs the node object passed programmatically.
    # We'll add a dummy argument for demonstration or potential future use (e.g., loading from file).
    parser.add_argument('--dummy_node_arg', type=str, help='Placeholder for future node loading mechanism (e.g., from file). Currently unused.')
    return parser.parse_args()


def main(args: argparse.Namespace):
    """
    Main execution function using parsed arguments.
    Note: This example creates a dummy node for demonstration.
    In practice, you would obtain the LogicNode object from elsewhere.
    """
    logger.info(f"Parsing arguments: {args}")

    # Example: Create a dummy node for demonstration purposes only.
    # In a real scenario, you would pass the actual root node object to the `show` method.
    # dummy_node = LogicNode(expression=LogicExpression.cast(True, repr="Dummy Root"))
    # ui_instance = DecisionTreeWebUi(host=args.host, port=args.port, debug=args.debug)
    # ui_instance.show(dummy_node)

    if args.dummy_node_arg:
        logger.warning("The --dummy_node_arg is currently a placeholder and does nothing.")

    logger.info("To use the UI, call `DecisionTreeWebUi.show(your_logic_node)` from your code.")
    logger.info(f"Example server config: Host={args.host}, Port={args.port}, Debug={args.debug}")


if __name__ == '__main__':
    args = parse()
    main(args)
