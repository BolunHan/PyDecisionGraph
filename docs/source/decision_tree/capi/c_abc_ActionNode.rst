c_abc.ActionNode
=================

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: ActionNode(LogicNode)

      A terminal node that can execute an optional ``action`` upon selection.

   .. py:attribute:: action

   .. py:method:: __init__(self, *, action: Callable[[], Any] = None, expression: object = None, dtype: type = None, repr: str = None, auto_connect: bool = True, **kwargs) -> None

      Initialize the ActionNode.

      :param action: An optional callable to execute when this action is selected (post-eval).
      :type action: Callable[[], Any] | None
      :param expression: A callable or static value (on-eval).
      :type expression: Union[Any, Callable[[], Any]]
      :param dtype: The expected type of the evaluated value (e.g. float, int, or bool).
      :type dtype: type, optional
      :param repr: A string representation of the expression.
      :type repr: str, optional
      :param auto_connect: If True, automatically connect this action node to the active node in the LGM upon creation.
      :type auto_connect: bool
      :param kwargs: __cinit__ extra kwargs guardian of for subclassing support.

   .. py:method:: __enter__(self) -> Never

      ActionNode does not support the context manager protocol.

      :raises NodeContextError: Using ``with ActionNode()`` is invalid.

   .. py:method:: append(self, child: ~decision_graph.decision_tree.capi.c_abc.LogicNode, condition: ~decision_graph.decision_tree.capi.c_abc.NodeEdgeCondition = AUTO_CONDITION) -> Never

      Appending children to an ActionNode is not supported.

      Since ActionNodes are terminal, ``replace`` and ``overwrite`` are also
      not applicable and will fail naturally if attempted.

      :raises TooManyChildren: Always raised to signal invalid operation.

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: PlaceholderNode(ActionNode)

      An action node that serves as a placeholder in the decision tree.

      This node is auto-generated and during evaluation returns itself in vigilant mode, otherwise returns a NoAction instance.


.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: NoAction(ActionNode)

      An action node whose evaluation returns itself and performs no action.

      :ivar sig: The signature marker for the no-action. Defaults to ``0``. Internal c fields is a ``ssize_t``
      :vartype sig: int

   .. py:attribute:: sig

   .. py:method:: __init__(self, *, sig: int = 0, action: Callable[[], Any] = None, expression: object = None, dtype: type = None, repr: str = None, auto_connect: bool = True, autogen: bool = False, **kwargs) -> None

      Initialize a NoAction node.

      :param sig: The signature marker for the no-action. Defaults to ``0``.
      :type sig: int
      :param action: An optional callable to execute when this action is selected (post-eval).
      :type action: Callable[[], Any] | None
      :param expression: A callable or static value (on-eval).
      :type expression: Union[Any, Callable[[], Any]]
      :param dtype: The expected type of the evaluated value (e.g. float, int, or bool).
      :type dtype: type, optional
      :param repr: A string representation of the expression.
      :type repr: str, optional
      :param auto_connect: If True, automatically connect this action node to the active node in the LGM upon creation.
      :type auto_connect: bool
      :param autogen: If True, marks this node as auto-generated.
      :type autogen: bool
      :param kwargs: __cinit__ extra kwargs guardian of for subclassing support.

   .. py:method:: __int__(self)

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: LongAction(ActionNode)

      An action node variant carrying a positive ``sig`` marker.

      :ivar sig: The signature marker for the long action. Defaults to ``1``.
      :vartype sig: int

   .. py:attribute:: sig

   .. py:method:: __init__(self, *, sig: int = 1, action: Callable[[], Any] = None, expression: object = None, dtype: type = None, repr: str = None, auto_connect: bool = True, **kwargs) -> None

      Initialize a LongAction node.

      :param sig: The signature marker for the long action. Defaults to ``1``.
      :type sig: int
      :param action: An optional callable to execute when this action is selected (post-eval).
      :type action: Callable[[], Any] | None
      :param expression: A callable or static value (on-eval).
      :type expression: Union[Any, Callable[[], Any]]
      :param dtype: The expected type of the evaluated value (e.g. float, int, or bool).
      :type dtype: type, optional
      :param repr: A string representation of the expression.
      :type repr: str, optional
      :param auto_connect: If True, automatically connect this action node to the active node in the LGM upon creation.
      :type auto_connect: bool
      :param kwargs: __cinit__ extra kwargs guardian of for subclassing support.

   .. py:method:: __int__(self)

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: ShortAction(ActionNode)

      An action node variant carrying a negative ``sig`` marker.

      :ivar sig: The signature marker for the long action. Defaults to ``-1``.
      :vartype sig: int

   .. py:attribute:: sig

   .. py:method:: __init__(self, *, sig: int = -1, action: Callable[[], Any] = None, expression: object = None, dtype: type = None, repr: str = None, auto_connect: bool = True, **kwargs) -> None

      Initialize a ShortAction node.

      :param sig: The signature marker for the short action. Defaults to ``-1``.
      :type sig: int
      :param action: An optional callable to execute when this action is selected (post-eval).
      :type action: Callable[[], Any] | None
      :param expression: A callable or static value (on-eval).
      :type expression: Union[Any, Callable[[], Any]]
      :param dtype: The expected type of the evaluated value (e.g. float, int, or bool).
      :type dtype: type, optional
      :param repr: A string representation of the expression.
      :type repr: str, optional
      :param auto_connect: If True, automatically connect this action node to the active node in the LGM upon creation.
      :type auto_connect: bool
      :param kwargs: __cinit__ extra kwargs guardian of for subclassing support.

   .. py:method:: __int__(self)

.. py:module:: decision_graph.decision_tree.capi.c_abc
   :no-index:

.. py:class:: CancelAction(ActionNode)

      An action node whose evaluation returns itself and performs cancel action.

   .. py:attribute:: sig

   .. py:method:: __init__(self, *, sig: int = 0, action: Callable[[], Any] = None, expression: object = None, dtype: type = None, repr: str = None, auto_connect: bool = True, **kwargs) -> None

      Initialize a CancelAction node.

      :param sig: The signature marker for the cancel action. Defaults to ``0``.
      :type sig: int
      :param action: An optional callable to execute when this action is selected (post-eval).
      :type action: Callable[[], Any] | None
      :param expression: A callable or static value (on-eval).
      :type expression: Union[Any, Callable[[], Any]]
      :param dtype: The expected type of the evaluated value (e.g. float, int, or bool).
      :type dtype: type, optional
      :param repr: A string representation of the expression.
      :type repr: str, optional
      :param auto_connect: If True, automatically connect this action node to the active node in the LGM upon creation.
      :type auto_connect: bool
      :param kwargs: __cinit__ extra kwargs guardian of for subclassing support.

   .. py:method:: __int__(self)

