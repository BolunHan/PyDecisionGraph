import enum

from cpython.object cimport PyObject
from cpython.unicode cimport PyUnicode_AsUTF8, PyUnicode_FromString
from libc.stdint cimport uintptr_t


class VarType(enum.IntEnum):
    raw_ptr = VAR_TYPE_RAW_PTR
    string = VAR_TYPE_STRING
    bool = VAR_TYPE_BOOL
    double = VAR_TYPE_DOUBLE
    int = VAR_TYPE_INT
    offset = VAR_TYPE_OFFSET
    time = VAR_TYPE_TIME
    date = VAR_TYPE_DATE
    datetime = VAR_TYPE_DATETIME
    d_vector = VAR_TYPE_D_VECTOR
    d_matrix = VAR_TYPE_D_MATRIX
    reserved = VAR_TYPE_RESERVED

    raw_ptr_ref = VAR_TYPE_RAW_PTR_REF
    string_ref = VAR_TYPE_STRING_REF
    bool_ref = VAR_TYPE_BOOL_REF
    double_ref = VAR_TYPE_DOUBLE_REF
    int_ref = VAR_TYPE_INT_REF
    offset_ref = VAR_TYPE_OFFSET_REF
    time_ref = VAR_TYPE_TIME_REF
    date_ref = VAR_TYPE_DATE_REF
    datetime_ref = VAR_TYPE_DATETIME_REF
    d_vector_ref = VAR_TYPE_D_VECTOR_REF
    d_matrix_ref = VAR_TYPE_D_MATRIX_REF
    inferred = VAR_TYPE_INFERRED

    raw_ptr_ref_ref = VAR_TYPE_RAW_PTR_REF_REF
    string_ref_ref = VAR_TYPE_STRING_REF_REF
    bool_ref_ref = VAR_TYPE_BOOL_REF_REF
    double_ref_ref = VAR_TYPE_DOUBLE_REF_REF
    int_ref_ref = VAR_TYPE_INT_REF_REF
    offset_ref_ref = VAR_TYPE_OFFSET_REF_REF
    time_ref_ref = VAR_TYPE_TIME_REF_REF
    date_ref_ref = VAR_TYPE_DATE_REF_REF
    datetime_ref_ref = VAR_TYPE_DATETIME_REF_REF
    d_vector_ref_ref = VAR_TYPE_D_VECTOR_REF_REF
    d_matrix_ref_ref = VAR_TYPE_D_MATRIX_REF_REF


class VarNumeric(enum.IntEnum):
    none = VAR_NUMERIC_NONE
    int = VAR_NUMERIC_INT
    double = VAR_NUMERIC_DOUBLE


cdef void c_dcg_var_pypack(dcg_var_t* out, object value) except *:
    cdef int ret_code

    if value is None:
        ret_code = c_dcg_var_init(out)
    elif isinstance(value, bool):
        ret_code = c_dcg_var_init_bool(out, value)
    elif isinstance(value, int):
        ret_code = c_dcg_var_init_int(out, <ssize_t> value)
    elif isinstance(value, float):
        ret_code = c_dcg_var_init_double(out, value)
    elif isinstance(value, str):
        ret_code = c_dcg_var_init_string(out, PyUnicode_AsUTF8(value))
    elif isinstance(value, bytes):
        ret_code = c_dcg_var_init_string(out, <const char*> value)
    else:
        ret_code = c_dcg_var_init_ptr(out, <void*> <PyObject*> value)

    if ret_code != DCG_OK:
        raise ValueError(f'Cannot pack {value!r} into a dcg_var_t.')


cdef object c_dcg_var_pyunpack(const dcg_var_t* var):
    if not var:
        return None
    if c_dcg_var_is_null(var):
        return None

    cdef dcg_var_type base = c_dcg_var_ref_base(var.dtype)

    if base == VAR_TYPE_BOOL:
        return c_dcg_var_as_bool(var)
    if base == VAR_TYPE_INT or base == VAR_TYPE_OFFSET:
        return c_dcg_var_as_int(var)
    if base == VAR_TYPE_DOUBLE:
        return c_dcg_var_as_double(var)
    if base == VAR_TYPE_STRING:
        return PyUnicode_FromString(c_dcg_var_as_string(var))
    return None


cdef class VarView:
    def __init__(self, uintptr_t address):
        self.header = <const dcg_var_t*> address

    @staticmethod
    cdef inline VarView c_from_header(const dcg_var_t* var):
        cdef VarView view = VarView.__new__(VarView)
        view.header = var
        return view

    def __repr__(self):
        if not self.header:
            return f'<{self.__class__.__name__} (Uninitialized)>'
        return f'<{self.__class__.__name__}({self.format()})>'

    def format(self):
        if not self.header:
            raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
        cdef char buf[DCG_VAR_STRING_MAXLEN]
        cdef int written = c_dcg_var_format(self.header, buf, sizeof(buf))
        if written < 0:
            raise RuntimeError(f'c_dcg_var_format failed with err code: {written}')
        return PyUnicode_FromString(buf)

    property address:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return <uintptr_t> self.header

    property dtype:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return VarType(self.header.dtype)

    property type_name:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return PyUnicode_FromString(c_dcg_var_type_name(self.header.dtype))

    property is_null:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_var_is_null(self.header)

    property is_ref:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_var_is_ref(self.header.dtype)

    property ref_level:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_var_ref_level(self.header.dtype)

    property ref_base:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return VarType(c_dcg_var_ref_base(self.header.dtype))

    property numeric:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return VarNumeric(c_dcg_var_numeric_of(self.header))

    property value:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_var_pyunpack(self.header)

    property as_bool:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_var_as_bool(self.header)

    property as_int:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_var_as_int(self.header)

    property as_double:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return c_dcg_var_as_double(self.header)

    property as_string:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            cdef const char* text = c_dcg_var_as_string(self.header)
            return None if not text else PyUnicode_FromString(text)

    property as_ptr:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return <uintptr_t> <void*> c_dcg_var_as_ptr(self.header)

    property as_ref:
        def __get__(self):
            if not self.header:
                raise RuntimeError(f'<{self.__class__.__name__}> not initialized!')
            return <uintptr_t> c_dcg_var_as_ref(self.header)
