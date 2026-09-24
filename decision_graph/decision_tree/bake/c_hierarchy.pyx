from cpython.unicode cimport PyUnicode_AsUTF8, PyUnicode_FromString
from libc.stdint cimport uintptr_t

from .c_allocator_protocol cimport DCG_DEFAULT_ALLOCATOR
from .c_node cimport NODE_REGISTRY, c_dcg_node_type_is_action
from .c_var cimport c_dcg_ret_code_name, c_dcg_var_pyunpack


cdef class RootLogicNode(LogicNode):
    def __init__(self, *, str name='Entry Point', bint inherit_contexts=False, **kwargs):
        cdef dcg_root_node* node = c_dcg_node_new_root(PyUnicode_AsUTF8(name), DCG_DEFAULT_ALLOCATOR)
        if not node:
            raise MemoryError('Failed to allocate the RootLogicNode.')

        self.header = &node.base
        self.owner = True
        self.name = name
        node.inherit_contexts = inherit_contexts
        # The record is the root's own field, so the view is taken once, over it:
        # what it reads is whatever the last walk left there.
        self.eval_path = NodeEvalPathView.c_from_header(&node.eval_path)
        self.c_register_node()

    # === Eval Protocol ===

    cdef void c_eval(self):
        if not self.header:
            raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')

        cdef dcg_root_node* root = <dcg_root_node*> self.header
        cdef int ret_code = c_dcg_root_node_eval(root)
        self.c_check_eval_code(ret_code, root.eval_path.failed)

    def eval(self):
        self.c_eval()

        cdef dcg_root_node* root = <dcg_root_node*> self.header
        cdef dcg_node* leaf = root.eval_path.leaf
        if not leaf:
            raise RuntimeError(f'{self.__class__.__name__} reached no leaf and reported no error.')

        if c_dcg_node_type_is_action(leaf.ntype):
            return NODE_REGISTRY[<uintptr_t> leaf]
        return c_dcg_var_pyunpack(&leaf.out)


cdef class NodeEvalPathView:
    def __init__(self, RootLogicNode node):
        self.header = <const dcg_node_eval_path*> &(<dcg_root_node*> node.header).eval_path

    @staticmethod
    cdef inline NodeEvalPathView c_from_header(const dcg_node_eval_path* path):
        cdef NodeEvalPathView view = NodeEvalPathView.__new__(NodeEvalPathView)
        view.header = path
        return view

    def __repr__(self):
        if not self.header:
            return f'<{self.__class__.__name__} (Uninitialized)>'
        cdef dcg_node_eval_path* path = <dcg_node_eval_path*> self.header
        return f'<{self.__class__.__name__}(nodes={path.n_nodes}, code={self.code_name}, leaf={self.leaf})>'

    def __len__(self):
        if not self.header:
            raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
        return (<dcg_node_eval_path*> self.header).n_nodes

    def __iter__(self):
        if not self.header:
            raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
        cdef dcg_node_eval_path* path = <dcg_node_eval_path*> self.header
        cdef size_t index
        for index in range(path.n_nodes):
            yield NODE_REGISTRY[<uintptr_t> path.node[index]]

    def __getitem__(self, Py_ssize_t index):
        cdef Py_ssize_t count = len(self)
        if index < 0:
            index += count
        if index < 0 or index >= count:
            raise IndexError(f'{self.__class__.__name__} index out of range')
        return NODE_REGISTRY[<uintptr_t> (<dcg_node_eval_path*> self.header).node[index]]

    # === Python Properties ===

    property address:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return <uintptr_t> self.header

    property nodes:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return list(self)

    property capacity:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return (<dcg_node_eval_path*> self.header).capacity

    property code:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return <int> (<dcg_node_eval_path*> self.header).code

    property code_name:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return PyUnicode_FromString(c_dcg_ret_code_name((<dcg_node_eval_path*> self.header).code))

    property leaf:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            cdef dcg_node* leaf = (<dcg_node_eval_path*> self.header).leaf
            return None if not leaf else NODE_REGISTRY[<uintptr_t> leaf]

    property failed:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            cdef dcg_node* failed = (<dcg_node_eval_path*> self.header).failed
            return None if not failed else NODE_REGISTRY[<uintptr_t> failed]

    property seq_id:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return (<dcg_node_eval_path*> self.header).seq_id


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
