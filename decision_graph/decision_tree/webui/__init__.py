from ..capi.c_abc import LogicNode
from .app import DecisionTreeWebUi


def show(root: LogicNode, **kwargs):
    app = DecisionTreeWebUi(
        host=kwargs.get('host', "127.0.0.1"),
        port=kwargs.get('port', 5000),
        debug=kwargs.get('debug', False)
    )
    app.show(root)
