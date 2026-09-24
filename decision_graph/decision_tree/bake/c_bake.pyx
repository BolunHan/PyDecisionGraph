from cpython.unicode cimport PyUnicode_FromString
from libc.stdint cimport uintptr_t

from .c_allocator_protocol cimport DCG_DEFAULT_ALLOCATOR
from .c_node cimport NODE_REGISTRY
from .c_var cimport c_dcg_ret_code_name

from ..exc import BakeFailureError


cdef class BakeReport:
    def __cinit__(self, *args, **kwargs):
        self.header = NULL
        self.owner = False

    def __init__(self, uintptr_t address=0, bint owner=False):
        if address:
            self.header = <dcg_bake_report*> address
            self.owner = owner
            return

        # No address: a report of its own, to be filled by the pass it is handed
        # to. That is what the door hands back, and what a caller who wants to
        # read a bake afterwards keeps.
        self.header = c_dcg_bake_report_new(DCG_DEFAULT_ALLOCATOR)
        if not self.header:
            raise MemoryError('Failed to allocate the BakeReport.')
        self.owner = True

    def __dealloc__(self):
        if self.owner and self.header:
            c_dcg_bake_report_free(self.header)
            self.header = NULL

    @staticmethod
    cdef BakeReport c_from_header(dcg_bake_report* header, bint owner=False):
        cdef BakeReport instance = BakeReport.__new__(BakeReport)
        instance.header = header
        instance.owner = owner
        return instance

    def __repr__(self):
        if not self.header:
            return f'<{self.__class__.__name__} (Uninitialized)>'
        return (
            f'<{self.__class__.__name__}('
            f'code={self.code_name}, node={self.node!r}, errors={self.header.errors}, '
            f'nodes={self.header.nodes}, depth={self.header.depth}, locked={self.header.locked}, '
            f'sealed={self.header.sealed}, capacity={self.header.capacity})>'
        )

    # === Python Properties ===

    property address:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return <uintptr_t> self.header

    property code:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return <int> self.header.code

    property code_name:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return PyUnicode_FromString(c_dcg_ret_code_name(self.header.code))

    property node:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            cdef dcg_node* node = self.header.node
            return None if not node else NODE_REGISTRY[<uintptr_t> node]

    property errors:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return self.header.errors

    property nodes:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return self.header.nodes

    property depth:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return self.header.depth

    property locked:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return self.header.locked

    property sealed:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return self.header.sealed

    property capacity:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return self.header.capacity


cdef inline str c_bake_failure_message(BakeReport report):
    """What to say about a bake the graph did not survive - the node, or the pass itself."""
    cdef str where = 'the pass itself' if report.node is None else f'{report.node!r}'
    return f'bake stopped at {where} with {report.code_name}: {report.errors} problem(s) found, nothing was locked'


cpdef BakeReport c_dcg_bake_root(RootLogicNode root, bint validate_only=False):
    if not root.header:
        raise RuntimeError(f'<{root.__class__.__name__}> not initialized!')

    # The report is the answer, so it is the wrapper's own block: the pass fills
    # it in place, and the failure below travels with it.
    cdef BakeReport answer = BakeReport()

    cdef dcg_bake_input input
    c_dcg_bake_input_init(&input)
    if validate_only:
        input.flags = DCG_BAKE_FLAG_VALIDATE_ONLY

    cdef int ret_code = c_dcg_root_node_bake(<dcg_root_node*> root.header, &input, answer.header)

    if ret_code != dcg_ret_code.DCG_OK:
        raise BakeFailureError(c_bake_failure_message(answer), report=answer)
    return answer
