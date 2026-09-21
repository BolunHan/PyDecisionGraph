from cpython.unicode cimport PyUnicode_AsUTF8

from .c_allocator_protocol cimport DCG_DEFAULT_ALLOCATOR
from .c_node import register_types


cdef class ActionNode(LogicNode):
    def __init__(self, dcg_node_type node_type, *, str repr=None, ssize_t sig=0, bint auto_connect=True, **kwargs):
        cdef dcg_action_node* node = c_dcg_node_connect_new_action(
            node_type,
            PyUnicode_AsUTF8(repr) if repr is not None else NULL,
            auto_connect,
            sig,
            NULL,
            LogicNode.c_get_manager(),
            DCG_DEFAULT_ALLOCATOR,
        )

        if not node:
            raise MemoryError(f'Failed to allocate {self.__class__.__name__}.')

        self.header = &node.base
        self.owner = True

        self.c_register_node()


cdef class NoAction(ActionNode):
    def __init__(self, *, ssize_t sig=0, str repr='NoAction', bint auto_connect=True, bint autogen=False, **kwargs):
        ActionNode.__init__(self, dcg_node_type.DCG_NODE_NOACTION, repr=repr, sig=sig, auto_connect=auto_connect)
        self.header.autogen = autogen

    def __int__(self):
        return 0


cdef class LongAction(ActionNode):
    def __init__(self, *, ssize_t sig=1, str repr='LongAction', bint auto_connect=True, **kwargs):
        ActionNode.__init__(self, dcg_node_type.DCG_NODE_LONGACTION, repr=repr, sig=sig, auto_connect=auto_connect)

    def __int__(self):
        return 1


cdef class ShortAction(ActionNode):
    def __init__(self, *, ssize_t sig=-1, str repr='ShortAction', bint auto_connect=True, **kwargs):
        ActionNode.__init__(self, dcg_node_type.DCG_NODE_SHORTACTION, repr=repr, sig=sig, auto_connect=auto_connect)

    def __int__(self):
        return -1


cdef class CancelAction(ActionNode):
    def __init__(self, *, ssize_t sig=0, str repr='CancelAction', bint auto_connect=True, **kwargs):
        ActionNode.__init__(self, dcg_node_type.DCG_NODE_CANCELACTION, repr=repr, sig=sig, auto_connect=auto_connect)

    def __int__(self):
        return 0


cdef class ClearAction(ActionNode):
    def __init__(self, *, ssize_t sig=0, str repr='ClearAction', bint auto_connect=True, **kwargs):
        ActionNode.__init__(self, dcg_node_type.DCG_NODE_CLEARACTION, repr=repr, sig=sig, auto_connect=auto_connect)

    def __int__(self):
        return 0


# What a rebuilt tree comes back as: the class each action type is wrapped in.
register_types({
    dcg_node_type.DCG_NODE_NOACTION: NoAction,
    dcg_node_type.DCG_NODE_LONGACTION: LongAction,
    dcg_node_type.DCG_NODE_SHORTACTION: ShortAction,
    dcg_node_type.DCG_NODE_CANCELACTION: CancelAction,
    dcg_node_type.DCG_NODE_CLEARACTION: ClearAction,
})
