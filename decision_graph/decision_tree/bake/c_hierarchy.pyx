from cpython.unicode cimport PyUnicode_AsUTF8
from libc.stdint cimport uintptr_t

from .c_allocator_protocol cimport DCG_DEFAULT_ALLOCATOR
from .c_node cimport DCG_NODE_BREAKPOINT, DCG_NODE_ROOT, LogicNode
from .c_node import register_types


cdef class RootLogicNode(LogicNode):
    def __init__(self, *, str name='Entry Point', bint inherit_contexts=False, **kwargs):
        cdef dcg_root_node* node = c_dcg_node_new_root(PyUnicode_AsUTF8(name), DCG_DEFAULT_ALLOCATOR)
        if not node:
            raise MemoryError('Failed to allocate the RootLogicNode.')

        self.header = &node.base
        self.owner = True
        self.name = name
        node.inherit_contexts = inherit_contexts
        self.c_register_node()


cdef class BreakpointNode(LogicNode):
    def __init__(self, *, object break_from=None, str repr=None, bint autogen=True, **kwargs):
        cdef dcg_logic_group* group = NULL
        cdef dcg_breakpoint_node* node
        if break_from:
            group = <dcg_logic_group*> <uintptr_t> break_from.address
        node = c_dcg_node_new_breakpoint(
            group,
            PyUnicode_AsUTF8(repr) if repr else NULL,
            DCG_DEFAULT_ALLOCATOR,
        )
        if not node:
            raise MemoryError('Failed to allocate the BreakpointNode.')

        self.header = &node.base
        self.owner = True
        self.break_from = break_from
        self.c_register_node()

    # === Python Properties ===

    property await_connection:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            cdef dcg_breakpoint_node* node = <dcg_breakpoint_node*> self.header
            return node.await_connection


# What a rebuilt tree comes back as: the class each special type is wrapped in.
register_types({
    DCG_NODE_ROOT: RootLogicNode,
    DCG_NODE_BREAKPOINT: BreakpointNode,
})
