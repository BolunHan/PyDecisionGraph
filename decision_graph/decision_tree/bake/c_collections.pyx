from cpython.unicode cimport PyUnicode_AsUTF8, PyUnicode_AsUTF8AndSize, PyUnicode_FromStringAndSize

from .c_allocator_protocol cimport DCG_DEFAULT_ALLOCATOR
from .c_logic_group cimport LGM
from .c_node cimport DCG_NODE_STRING_MAXLEN
from .c_var cimport c_dcg_var_pypack, c_dcg_var_pyunpack, dcg_ret_code, dcg_var_type


cdef class LogicMapping(LogicGroup):
    def __init__(self, *, str name=None, size_t capacity=0, LogicGroup parent=None, **kwargs):
        if name is not None and len(name) > DCG_NODE_STRING_MAXLEN // 4:
            raise ValueError(
                f'A mapping name is part of its reads\' display text: {len(name)} characters leaves no room '
                f'for an entry name. Keep it under {DCG_NODE_STRING_MAXLEN // 4}.'
            )

        cdef dcg_mapping_lgroup* lgroup = c_dcg_mapping_lgroup_new(
            PyUnicode_AsUTF8(name) if name is not None else NULL,
            capacity,
            DCG_DEFAULT_ALLOCATOR,
        )
        if not lgroup:
            raise MemoryError(f'Failed to allocate {self.__class__.__name__}({name!r}).')

        # The base group is the first member, so a mapping IS a group.
        self.header = <dcg_logic_group*> lgroup
        self.owner = True
        if parent is not None:
            self.header.parent = parent.header
        LGM.register(self)

    cdef void c_free_header(self):
        c_dcg_mapping_lgroup_free(<dcg_mapping_lgroup*> self.header)

    # === Cython Internal Binding ===

    cdef dcg_var_t* c_get_slot(self, const char* key, size_t key_len):
        return c_dcg_mapping_lgroup_get_slot(<dcg_mapping_lgroup*> self.header, key, key_len)

    cdef dcg_var_t* c_get_create_slot(self, const char* key, size_t key_len, const dcg_var_type* var_type) except NULL:
        cdef dcg_mapping_lgroup* lgroup = <dcg_mapping_lgroup*> self.header
        cdef dcg_var_t*          slot   = NULL
        cdef int                 ret_code = c_dcg_mapping_lgroup_get_create_slot(lgroup, key, key_len, var_type, &slot)

        if ret_code != dcg_ret_code.DCG_OK:
            if ret_code == dcg_ret_code.DCG_ERR_BUSY:
                raise KeyError(f'<{self.__class__.__name__}> is frozen: no entry named {PyUnicode_FromStringAndSize(key, key_len)!r}.')
            raise RuntimeError(f'c_dcg_mapping_lgroup_get_create_slot failed with err code: {ret_code}')
        return slot

    # === Python Interfaces ===

    def __getitem__(self, str key):
        return AttrExpression(key)

    def __getattr__(self, str key):
        return AttrExpression(key)

    def __setitem__(self, str key, object value):
        cdef Py_ssize_t  key_len  = 0
        cdef const char* c_key    = PyUnicode_AsUTF8AndSize(key, &key_len)
        cdef dcg_var_t   packed

        c_dcg_var_pypack(&packed, value)

        cdef int ret_code = c_dcg_mapping_lgroup_set(<dcg_mapping_lgroup*> self.header, c_key, key_len, &packed)
        if ret_code != dcg_ret_code.DCG_OK:
            raise RuntimeError(f'c_dcg_mapping_lgroup_set failed with err code: {ret_code}')

    def __contains__(self, str key):
        cdef Py_ssize_t  key_len = 0
        cdef const char* c_key   = PyUnicode_AsUTF8AndSize(key, &key_len)
        return c_dcg_mapping_lgroup_get_slot(<dcg_mapping_lgroup*> self.header, c_key, key_len) != NULL

    def __len__(self):
        return (<dcg_mapping_lgroup*> self.header).n_slots

    property frozen:
        def __get__(self):
            if not self.header:
                return False
            return bool((<dcg_mapping_lgroup*> self.header).frozen)

        def __set__(self, bint value):
            (<dcg_mapping_lgroup*> self.header).frozen = value


cdef class AttrExpression(VariableNode):
    def __init__(self, str name):
        cdef object       active  = LGM.active_group
        cdef LogicMapping mapping
        cdef Py_ssize_t   key_len = 0

        if active is None:
            raise RuntimeError(f'No logic group is active: a {self.__class__.__name__} reads the store a build runs inside.')
        if not isinstance(active, LogicMapping):
            raise TypeError(f'The active group is a {type(active).__name__}, not a LogicMapping: there is no store to read {name!r} from.')

        mapping = <LogicMapping> active

        cdef const char* key = PyUnicode_AsUTF8AndSize(name, &key_len)

        # A read names its entry, so the entry is created when the name is new -
        # reserved, since the read names no type of its own.
        mapping.c_get_create_slot(key, key_len, NULL)

        cdef dcg_variable_node* node = c_dcg_mapping_lgroup_get_node(<dcg_mapping_lgroup*> mapping.header, key, key_len, DCG_DEFAULT_ALLOCATOR)
        if not node:
            raise MemoryError(f'Failed to allocate the {self.__class__.__name__}.')

        self.header = &node.base
        # The group owns the reads built over its store - it is what allocates
        # them - so this wrapper releases nothing, and it holds the group instead,
        # which is what keeps the store alive for as long as the read is.
        self.owner = False
        self.logic_group = mapping
        self.c_register_node()

    # === Python Properties ===

    property value:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')

            cdef dcg_variable_node* node = <dcg_variable_node*> self.header
            if not node.key:
                return None

            cdef dcg_mapping_lgroup* lgroup = <dcg_mapping_lgroup*> (<LogicMapping> self.logic_group).header
            cdef dcg_var_t* slot = &node.base.out

            # The read knows WHERE its entry is: until it has been evaluated that
            # is an offset in its own slot - no name lookup, and nothing that a
            # growth of the store can invalidate - and from then on the slot IS
            # the entry, read through.
            if slot.dtype == dcg_var_type.VAR_TYPE_INFERRED or slot.dtype == dcg_var_type.VAR_TYPE_OFFSET:
                return c_dcg_var_pyunpack(&lgroup.slots[slot.value.as_offset])
            return c_dcg_var_pyunpack(slot)
