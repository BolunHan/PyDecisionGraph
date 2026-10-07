"""The web UI of the pure-Python layer (``decision_graph.decision_tree.native``).

The tree it draws is a native ``LogicNode`` tree, read through the native
surface alone — see ``.app`` for the one place that surface is named.
"""

import logging

from .. import LOGGER
from decision_graph.decision_tree.native import LogicNode, RootLogicNode

LOGGER = LOGGER.getChild('WebUI.Native')

from . import app
from .app import DecisionTreeWebUi


def show(root: LogicNode, with_eval: bool = True, **kwargs):
    _app = DecisionTreeWebUi(
        host=kwargs.get('host', "127.0.0.1"),
        port=kwargs.get('port', 5000),
        debug=kwargs.get('debug', False)
    )
    _app.show(
        node=root,
        with_eval=with_eval
    )


def watch(root: RootLogicNode, interval: float = .5, block: bool = False, **kwargs):
    _app = DecisionTreeWebUi(
        host=kwargs.get('host', "127.0.0.1"),
        port=kwargs.get('port', 5000),
        debug=kwargs.get('debug', False)
    )
    _app.watch(
        node=root,
        interval=interval,
        block=block
    )


def to_html(root: LogicNode, file_name: str, with_eval: bool = True):
    DecisionTreeWebUi.to_html(
        node=root,
        file_name=file_name,
        with_eval=with_eval
    )


def set_logger(logger: logging.Logger):
    global LOGGER
    LOGGER = logger.getChild('WebUI.Native')
    app.LOGGER = LOGGER
