from cpython.dict cimport PyDict_Contains
from cpython.unicode cimport PyUnicode_AsUTF8, PyUnicode_FromString
from libc.stdint cimport uintptr_t
from libc.string cimport strlen

from cbase.bytemap cimport c_bytemap_gen_seq_id

from .c_allocator_protocol cimport DCG_DEFAULT_ALLOCATOR
from .c_var cimport dcg_ret_code


cdef class LogicGroupNameRegistry(BoundByteMap):
    @staticmethod
    cdef LogicGroupNameRegistry c_from_header(dcg_logic_group_manager* header, bint owner=False):
        if owner:
            raise PermissionError('Must not assign dcg_logic_group_manager ownership to a <LogicGroupNameRegistry>')
        cdef LogicGroupNameRegistry instance = LogicGroupNameRegistry.__new__(LogicGroupNameRegistry)
        instance.owner = False
        instance.seq_id = c_bytemap_gen_seq_id(<void*> instance)
        instance.c_bind(&header.registry)
        return instance

    cdef const char* c_serialize_value(self, object obj, size_t* value_len):
        cdef LogicGroup logic_group = <LogicGroup> obj
        self._ws_ptr = <void*> logic_group.header
        if value_len:
            value_len[0] = sizeof(void*)
        return <const char*> &self._ws_ptr

    cdef object c_deserialize_value(self, const char* value, size_t value_len):
        cdef void** vp = <void**> value
        cdef dcg_logic_group* logic_group = <dcg_logic_group*> vp[0]
        if not logic_group:
            return None
        return LogicGroup.c_from_header(logic_group, False)


cdef class LogicGroupManager:
    def __init__(self):
        self.header = c_dcg_lgm_new(DCG_DEFAULT_ALLOCATOR)
        if not self.header:
            raise MemoryError('Failed to allocate the LogicGroupManager.')

        self.owner = True
        self.registry = LogicGroupNameRegistry.c_from_header(self.header, False)
        self.node_stack = []

    def __dealloc__(self):
        if not self.owner:
            return

        if self.header:
            c_dcg_lgm_free(self.header)

    # === Cython Internal Binding ===

    cdef void c_register(self, dcg_logic_group* group):
        cdef int ret_code = c_dcg_lgm_register(self.header, group)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_lgm_register failed with err code: {ret_code}')
        self.registry[PyUnicode_FromString(group.name)] = LogicGroup.c_from_header(group, False)

    cdef dcg_logic_group* c_find(self, const char* name):
        if not name:
            return NULL
        return c_dcg_lgm_find(self.header, name, strlen(name))

    cdef void c_enter_group(self, dcg_logic_group* group):
        cdef int ret_code = c_dcg_lgm_enter_group(self.header, group)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_lgm_enter_group failed with err code: {ret_code}')

    cdef void c_exit_group(self, dcg_logic_group* group):
        cdef int ret_code = c_dcg_lgm_exit_group(self.header, group)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_lgm_exit_group failed with err code: {ret_code}')

    cdef int c_label_node(self, dcg_node* node):
        cdef int added = c_dcg_lgm_label_node(self.header, node)
        if added < 0:
            raise RuntimeError(f'c_dcg_lgm_label_node failed with err code: {added}')
        return added

    cdef void c_break_inspection(self, dcg_logic_group* group):
        cdef int ret_code = c_dcg_lgm_break_inspection(self.header, group)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_lgm_break_inspection failed with err code: {ret_code}')

    cdef void c_shelve(self):
        cdef int ret_code = c_dcg_lgm_shelve(self.header)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_lgm_shelve failed with err code: {ret_code}')

    cdef void c_unshelve(self):
        cdef int ret_code = c_dcg_lgm_unshelve(self.header)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_lgm_unshelve failed with err code: {ret_code}')

    # === Python Interfaces ===

    cpdef void node_stack_append(self, LogicNode node):
        self.node_stack.append(node)

    cpdef void node_stack_pop(self, LogicNode node):
        if self.node_stack and self.node_stack[-1] is node:
            self.node_stack.pop()

    def register(self, LogicGroup group):
        if group.name is not None:
            self.c_register(group.header)
        GROUP_REGISTRY[<uintptr_t> group.header] = group

    def find(self, str name):
        return self.registry[name]

    def shelve(self):
        self.c_shelve()

    def unshelve(self):
        self.c_unshelve()

    def clear(self):
        c_dcg_lgm_clear(self.header)

    def label_node(self, LogicNode node):
        return self.c_label_node(node.header)

    # === Python Properties ===

    property inspection_mode:
        def __get__(self):
            return self.header.inspection_mode

        def __set__(self, bint value):
            self.header.inspection_mode = value

    property vigilant_mode:
        def __get__(self):
            return self.header.vigilant_mode

        def __set__(self, bint value):
            self.header.vigilant_mode = value

    property active_group:
        def __get__(self):
            cdef dcg_logic_group* active = c_dcg_lgm_active_group(self.header)
            if active == NULL:
                return None
            return GROUP_REGISTRY[<uintptr_t> active]

    property active_node:
        def __get__(self):
            if not self.node_stack:
                return None
            return self.node_stack[-1]

    property address:
        def __get__(self):
            return <uintptr_t> self.header

    property n_groups:
        def __get__(self):
            return self.header.n_groups

    property n_nodes:
        def __get__(self):
            return self.header.n_nodes

    property n_breakpoints:
        def __get__(self):
            return c_dcg_lgm_breakpoint_count(self.header)


cdef class LogicGroup:
    def __init__(self, *, str name=None, LogicGroup parent=None, **kwargs):
        self.header = c_dcg_logic_group_new(
            DCG_LG_BASE,
            PyUnicode_AsUTF8(name) if name is not None else NULL,
            DCG_DEFAULT_ALLOCATOR,
        )
        if not self.header:
            raise MemoryError(f'Failed to allocate {self.__class__.__name__}({name!r}).')

        self.owner = True
        if parent is not None:
            self.header.parent = parent.header
        LGM.register(self)

    def __dealloc__(self):
        # __dealloc__ chains through the hierarchy, so the release goes through
        # one virtual method: a family that adds state of its own overrides it
        # rather than freeing the block a second time in its own __dealloc__.
        if not self.owner:
            return

        if self.header:
            self.c_free_header()

    cdef void c_free_header(self):
        c_dcg_logic_group_free(self.header)

    @staticmethod
    cdef LogicGroup c_from_header(dcg_logic_group* header, bint owner=False):
        cdef LogicGroup instance = LogicGroup.__new__(LogicGroup)
        instance.header = header
        instance.owner = owner
        return instance

    # === Python Dunders ===

    def __repr__(self):
        return f'<{self.__class__.__name__}({self.name!r})>'

    def __enter__(self):
        LGM.c_enter_group(self.header)
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        LGM.c_exit_group(self.header)
        return False

    # === Cython Internal Binding ===

    cdef void c_break(self, dcg_logic_group* scope):
        LGM.c_break_inspection(scope)

    # === Python Interfaces ===

    def break_(self, LogicGroup scope=None):
        self.c_break(scope.header if scope is not None else self.header)

    @classmethod
    def break_active(cls, LogicGroup scope=None):
        if scope is not None:
            LGM.c_break_inspection(scope.header)

    # === Python Properties ===

    property name:
        def __get__(self):
            if not self.header or not self.header.name:
                return None
            return PyUnicode_FromString(self.header.name)

    property parent:
        def __get__(self):
            if not self.header or not self.header.parent:
                return None
            return GROUP_REGISTRY[<uintptr_t> self.header.parent]

    property lgtype:
        def __get__(self):
            if not self.header:
                return None
            return self.header.lgtype

    property address:
        def __get__(self):
            return <uintptr_t> self.header


cdef class LogicGroupWrapperRegistry(BoundByteMap):
    @staticmethod
    cdef LogicGroupWrapperRegistry c_from_header(bytemap* header, bint owner=False):
        cdef LogicGroupWrapperRegistry instance = LogicGroupWrapperRegistry.__new__(LogicGroupWrapperRegistry)
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
        cdef LogicGroup logic_group = <LogicGroup> obj
        self._ws_ptr = <void*> logic_group.header
        if value_len:
            value_len[0] = sizeof(void*)
        return <const char*> &self._ws_ptr

    cdef object c_deserialize_value(self, const char* value, size_t value_len):
        cdef void** vp = <void**> value
        cdef dcg_logic_group* logic_group = <dcg_logic_group*> vp[0]
        if not logic_group:
            return None
        return LogicGroup.c_from_header(logic_group, False)

    def __getitem__(self, object key):
        cdef int ret_code = PyDict_Contains(<dict> self, key)
        if ret_code == 0:
            return LogicGroup.c_from_header(<dcg_logic_group*> <uintptr_t> key, False)
        return super().__getitem__(key)


cdef LogicGroupWrapperRegistry GROUP_REGISTRY = LogicGroupWrapperRegistry()
globals()['GROUP_REGISTRY'] = GROUP_REGISTRY

LGM = LogicGroupManager()
globals()['LGM'] = LGM
