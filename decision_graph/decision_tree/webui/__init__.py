import logging

from .. import LOGGER, RootLogicNode

LOGGER = LOGGER.getChild('WebUI')

from .app import DecisionTreeWebUi
from .. import LogicNode


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
    LOGGER = logger.getChild('WebUI')
    app.LOGGER = LOGGER
