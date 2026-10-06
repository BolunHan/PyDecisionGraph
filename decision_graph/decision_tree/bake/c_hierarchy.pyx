from cpython.unicode cimport PyUnicode_AsUTF8, PyUnicode_FromString
from libc.stdint cimport uintptr_t

from .c_allocator_protocol cimport DCG_DEFAULT_ALLOCATOR
from .c_edge cimport NO_CONDITION
from .c_node cimport NODE_REGISTRY
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

        return c_dcg_var_pyunpack(&leaf.out)

    def __call__(self):
        return self.eval()

    # === Bake Protocol ===

    cpdef object bake(self, bint validate_only=False):
        # The protocol lives in a module above this one - the bake pass is asked
        # of a root and reads every family to answer - so the reach is the
        # module's Python door at call time, never a cimport (DEPENDENCY.md 4.3).
        from .c_bake import c_dcg_bake_root
        return c_dcg_bake_root(self, validate_only)

    def get_breakpoint(self):
        """The breakpoint this graph left waiting, or None if it left none.

        A breakpoint that has not resumed into anything yet is where a branch
        stopped, and that is what this looks for - the place a build can take up
        again. What makes a graph assemblable from several places is exactly
        this: one function builds up to a break, this hands the break back, and
        the next function takes it and builds the rest, so the two halves never
        have to be in the same scope, or even the same function.

        The search itself is the C layer's - ``c_dcg_node_get_breakpoint`` walks
        the graph's own links. Nothing here needs the Python side to have kept a
        wrapper for every node the walk crosses, which is what makes it work on a
        graph the build only partly came through Python.

        Returns:
            The waiting breakpoint, or None when the graph has none.
        """
        if not self.header:
            raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')

        cdef dcg_breakpoint_node* found = c_dcg_node_get_breakpoint(self.header)
        if not found:
            return None
        return NODE_REGISTRY[<uintptr_t> &found.base]

    property inherit_contexts:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return bool((<dcg_root_node*> self.header).inherit_contexts)


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

    # === Python Interfaces ===

    @classmethod
    def break_(cls, object break_from, **kwargs):
        """Break out of a group, and hand back the breakpoint that did it.

        A break is a node: it takes the arm the build was about to fill, and the
        build continues from IT rather than from the group it left. That is what
        makes this the door for assembling a graph in pieces - one function
        builds up to the break and returns, and the break it made is entered here
        to carry on outside the group.

        The breakpoint is built first and then installed, rather than made by the
        install: the block that lands in the graph has to be the one this wrapper
        holds, or the caller would be handed a handle on a node that is not in
        the graph.

        Args:
            break_from: The group being broken out of.
            **kwargs: Passed to the constructor.

        Returns:
            The breakpoint, now in the graph and waiting to resume into the next
            node entered.

        Raises:
            RuntimeError: When the C layer cannot place it.
            MemoryError: When the block cannot be allocated.
        """
        cdef BreakpointNode breakpoint = cls(break_from=break_from, **kwargs)

        cdef int ret_code = c_dcg_lgm_install_breakpoint(
            LogicNode.c_get_manager(),
            <dcg_breakpoint_node*> breakpoint.header,
        )
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_lgm_install_breakpoint failed with err code: {ret_code}')
        return breakpoint

    def connect(self, LogicNode child):
        """Connect a node as what this breakpoint resumes into.

        A breakpoint resumes into exactly one node, which is what its arm is for,
        so a second connection is refused by the C layer rather than replacing
        the first - see ``append``.
        """
        self.append(child, NO_CONDITION)

    # === Python Properties ===

    property linked_to:
        def __get__(self):
            """The node this breakpoint resumes into, or None while it waits."""
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            for child in self.children.values():
                return child
            return None

    property await_connection:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            cdef dcg_breakpoint_node* node = <dcg_breakpoint_node*> self.header
            return node.await_connection
