import logging

from .app import DecisionTreeWebUi
from .. import LOGGER, LogicNode

LOGGER = LOGGER.getChild('WebUI')


def show(root: LogicNode, **kwargs):
    _app = DecisionTreeWebUi(
        host=kwargs.get('host', "127.0.0.1"),
        port=kwargs.get('port', 5000),
        debug=kwargs.get('debug', False)
    )
    _app.show(root, **kwargs)


def to_html(root: LogicNode, file_name: str, with_eval: bool = True):
    _app = DecisionTreeWebUi(host="127.0.0.1", port=5000, debug=False)
    _app.to_html(root, file_name, with_eval)


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger.getChild('WebUI')
    app.LOGGER = LOGGER
