from cpython.bytes cimport PyBytes_AsString, PyBytes_FromStringAndSize
from cpython.dict cimport PyDict_Contains
from cpython.object cimport PyObject
from cpython.unicode cimport PyUnicode_AsUTF8, PyUnicode_FromString, PyUnicode_FromStringAndSize
from libc.stdint cimport uintptr_t

from uuid import UUID

from cbase.bytemap.c_bytemap cimport c_bytemap_gen_seq_id

from ..exc import NodeTypeError

from .c_allocator_protocol cimport DCG_DEFAULT_ALLOCATOR
from .c_var cimport dcg_ret_code
from .c_edge cimport EDGE_REGISTRY, NodeEdgeCondition, dcg_node_edge_condition, NO_CONDITION, C_AUTO_CONDITION


cdef size_t DCG_RENDER_BUFSIZE = 1 << 16

# Filled in by every module that owns a node family, at import. The map lives
# here because every module needs the wrapping helper and none of them owns it:
# a module registering its types is how a rebuilt tree comes back as the classes
# the build used, without this module importing anybody. A plain Python import
# of `register_types` is not an edge in the pxd graph, which is what keeps a
# family module reachable from here (DEPENDENCY.md 4.2).
cdef dict TYPE_CLASSES = {}


def register_types(dict mapping):
    TYPE_CLASSES.update(mapping)


cdef inline type c_class_for_type(dcg_node_type node_type):
    """The wrapper class a node type is rebuilt as.

    The exact type is asked first, then its family head, and finally the base -
    so a node from a family this package does not know yet still comes back as
    something readable rather than as a bare pointer.
    """
    cdef type found = TYPE_CLASSES.get(<int> node_type)
    if found is None:
        found = TYPE_CLASSES.get(<int> (node_type & DCG_NODE_FAMILY_MASK))
    if found is None:
        found = LogicNode
    return found


cdef class LogicNode:
    def __cinit__(self, *args, **kwargs):
        self.children = {}
        self.condition_to_parent = NO_CONDITION
        # self.callback_id = 0

    def __init__(self, *, str repr=None, object uid=None, **kwargs):
        raise NodeTypeError(
            f'{self.__class__.__name__} is the node base: bake has no type for a bare node yet. '
            'Build an action leaf (NoAction, LongAction, ...), a root, or an expression node.'
        )

    def __dealloc__(self):
        if not self.owner:
            return

        if self.header:
            if self.callback_id:
                c_dcg_node_unregister_callback(self.header, self.callback_id)
            c_dcg_node_free_generic(self.header)

    @staticmethod
    cdef inline LogicNode c_from_header(dcg_node* header, bint owner=False):
        cdef type cls = c_class_for_type(header.ntype)
        cdef LogicNode instance = <LogicNode> cls.__new__(cls)
        instance.header = header
        instance.owner = owner

        # The registry is keyed by the header address, so the parent is looked
        # up by ITS header - a wrapper is not an address, and a parentless node
        # has none to look up.
        cdef uintptr_t parent_addr = 0
        instance.parent = None
        if header.parent:
            parent_addr = <uintptr_t> header.parent
            if parent_addr in NODE_REGISTRY:
                instance.parent = NODE_REGISTRY[parent_addr]
        return instance

    @staticmethod
    cdef inline dcg_logic_group_manager* c_get_manager():
        if not C_LGM:
            from .c_logic_group import LGM as _LGM
            global C_LGM, LGM
            C_LGM = <dcg_logic_group_manager*> <uintptr_t> _LGM.address
            LGM = _LGM
        return C_LGM

    @staticmethod
    def get_manager():
        if not LGM:
            LogicNode.c_get_manager()
        return LGM

    cdef inline void c_register_node(self):
        NODE_REGISTRY[<uintptr_t> self.header] = self

        cdef int ret_code = c_dcg_node_register_callback(self.header, LogicNode.c_node_callback_event_adaptor, <void*> <PyObject*> self, &self.callback_id)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_node_register_callback failed with err code: {ret_code}')

    # === Cython Internal Binding ===

    @staticmethod
    cdef void c_node_callback_event_adaptor(dcg_node_event event, dcg_node* node, dcg_node* subject, uint64_t seq_id, void* user_data) noexcept:
        if not node or not user_data:
            return

        cdef LogicNode wrapper = <LogicNode> <PyObject*> user_data
        cdef LogicNode child
        cdef NodeEdgeCondition condition

        if event == DCG_NODE_EVENT_CHILD_ADDED or event == DCG_NODE_EVENT_CHILD_UPDATED:
            child = NODE_REGISTRY[<uintptr_t> subject]
            condition = EDGE_REGISTRY[<uintptr_t> subject.condition_to_parent]
            wrapper.children[condition] = child
            child.parent = wrapper
            child.condition_to_parent = condition
        elif event == DCG_NODE_EVENT_CHILD_REMOVED:
            for condition, child in wrapper.children.items():
                if <uintptr_t> child.header == <uintptr_t> subject:
                    break
            else:
                raise BufferError(f'Node {<uintptr_t> node:#0x} not found!')
            wrapper.children.pop(condition)
            child = NODE_REGISTRY[<uintptr_t> subject]
            child.parent = None
            child.condition_to_parent = NO_CONDITION
        elif event == DCG_NODE_EVENT_CHILD_CLEARED:
            wrapper.children.clear()

    cdef void c_append(self, dcg_node* child, dcg_node_edge_condition* condition):
        cdef int ret_code = c_dcg_node_append(self.header, child, condition)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_node_append failed with err code: {ret_code}')

    cdef void c_replace(self, dcg_node* old_node, dcg_node* new_node):
        cdef int ret_code = c_dcg_node_replace(old_node, new_node)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_node_replace failed with err code: {ret_code}')

    cdef void c_detach(self):
        cdef int ret_code = c_dcg_node_detach(self.header)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_node_detach failed with err code: {ret_code}')

    cdef str c_render(self, int max_depth, bint show_labels, bint show_out, str style):
        cdef dcg_render_opts opts
        cdef size_t cap = DCG_RENDER_BUFSIZE
        cdef size_t used
        cdef bytes buffer
        cdef char* out

        c_dcg_render_opts_default(&opts)
        opts.max_depth = max_depth
        opts.show_labels = show_labels
        opts.show_out = show_out
        opts.style = DCG_RENDER_UNICODE if style == 'unicode' else DCG_RENDER_ASCII

        buffer = PyBytes_FromStringAndSize(NULL, <Py_ssize_t> cap)
        out = PyBytes_AsString(buffer)
        used = c_dcg_node_render_to_string(self.header, out, cap, &opts)
        if used >= cap:
            raise BufferError(f'The render needs {used} bytes; the {cap}-byte buffer is too small.')
        return PyUnicode_FromStringAndSize(out, used)

    # === Python Dunders ===

    def __repr__(self):
        if not self.header:
            return f'<{self.__class__.__name__} (Uninitialized)>'
        return f'<{self.__class__.__name__}({self.repr!r})>'

    def __rshift__(self, LogicNode other):
        self.c_append(other.header, C_AUTO_CONDITION)
        return other

    def __enter__(self):
        cdef int ret_code = c_dcg_lgm_enter_node(LogicNode.c_get_manager(), self.header)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_lgm_enter_node failed with err code: {ret_code}')
        self.get_manager().node_stack_append(self)
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        cdef int ret_code = c_dcg_lgm_exit_node(LogicNode.c_get_manager(), self.header)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_lgm_exit_node failed with err code: {ret_code}')
        self.get_manager().node_stack_pop(self)
        return False

    # === Python Interfaces ===

    def append(self, LogicNode child, NodeEdgeCondition condition=None):
        self.c_append(child.header, condition.header if condition is not None else C_AUTO_CONDITION)

    def overwrite(self, LogicNode new_node, NodeEdgeCondition condition):
        cdef LogicNode original = self.children.get(condition)
        if original is None:
            raise KeyError(f'Edge {condition} not registered, cannot overwrite.')
        self.c_replace(original.header, new_node.header)

    def replace(self, LogicNode original_node, LogicNode new_node):
        self.c_replace(original_node.header, new_node.header)

    def detach(self):
        self.c_detach()

    def label(self, str name):
        cdef int ret_code = c_dcg_node_add_label(self.header, PyUnicode_AsUTF8(name))
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_node_add_label failed with err code: {ret_code}')

    def unlabel(self, str name):
        cdef int ret_code = c_dcg_node_remove_label(self.header, PyUnicode_AsUTF8(name))
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_node_remove_label failed with err code: {ret_code}')

    def has_label(self, str name):
        return bool(c_dcg_node_has_label(self.header, PyUnicode_AsUTF8(name)))

    def render(self, int max_depth=0, bint show_labels=True, bint show_out=False, str style='unicode'):
        return self.c_render(max_depth, show_labels, show_out, style)

    def validate(self):
        cdef dcg_validate_report report
        if c_dcg_node_validate(self.header, &report):
            return None
        return report.code, report.errors, report.nodes, report.depth

    # === Python Properties ===

    property repr:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            if not self.header.repr:
                return None
            return PyUnicode_FromString(self.header.repr)

    property type:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return PyUnicode_FromString(c_dcg_node_type_name(self.header.ntype))

    property uuid:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return UUID(bytes=self.header.uid[:16])

    property autogen:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return bool(self.header.autogen)

    property is_leaf:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_node_is_leaf(self.header)

    property labels:
        def __get__(self):
            cdef list out = []
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            cdef dcg_node_label* entry = self.header.labels
            while entry != NULL:
                if entry.label != NULL:
                    out.append(PyUnicode_FromString(entry.label))
                entry = entry.next
            return out

    property size:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_node_subtree_size(self.header)

    property address:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return <uintptr_t> self.header


cdef class PlaceholderNode(LogicNode):
    def __init__(self, **kwargs):
        cdef dcg_node* node = c_dcg_node_new_placeholder(DCG_DEFAULT_ALLOCATOR)
        if not node:
            raise MemoryError('Failed to allocate a PlaceholderNode.')

        self.header = node
        self.owner = True
        self.c_register_node()


cdef class LogicNodeRegistry(BoundByteMap):
    @staticmethod
    cdef LogicNodeRegistry c_from_header(bytemap* header, bint owner=False):
        cdef LogicNodeRegistry instance = LogicNodeRegistry.__new__(LogicNodeRegistry)
        instance.owner = owner
        instance.seq_id = c_bytemap_gen_seq_id(<void*> instance)
        instance.c_bind(header)
        return instance

    cdef const char* c_serialize_key(self, object obj, size_t* key_len):
        # A header address is provided in format of int (uintptr_t)
        if isinstance(obj, int):
            self._ws_key_buf = <void*> <uintptr_t> obj
            if key_len:
                key_len[0] = sizeof(void*)
            return <const char*> &self._ws_key_buf
        return BoundByteMap.c_serialize_key(self, obj, key_len)

    cdef object c_deserialize_key(self, const char* key, size_t key_len):
        return <uintptr_t> <void*> key

    cdef const char* c_serialize_value(self, object obj, size_t* value_len):
        cdef LogicNode node = <LogicNode> obj
        self._ws_ptr = <void*> node.header
        if value_len:
            value_len[0] = sizeof(void*)
        return <const char*> &self._ws_ptr

    cdef object c_deserialize_value(self, const char* value, size_t value_len):
        cdef void** vp = <void**> value
        cdef dcg_node* node = <dcg_node*> vp[0]
        return LogicNode.c_from_header(node, False)

    def __getitem__(self, object key):
        cdef int ret_code = PyDict_Contains(<dict> self, key)
        if ret_code == 0:
            return LogicNode.c_from_header(<dcg_node*> <uintptr_t> key, False)
        return super().__getitem__(key)


# Local Cached LGM Pointer
cdef dcg_logic_group_manager* C_LGM = NULL
cdef object LGM = None

cdef LogicNodeRegistry NODE_REGISTRY = LogicNodeRegistry()
globals()['NODE_REGISTRY'] = NODE_REGISTRY

# The one family this module owns is the node layer's own: the placeholder.
register_types({DCG_NODE_PLACEHOLDER: PlaceholderNode})
