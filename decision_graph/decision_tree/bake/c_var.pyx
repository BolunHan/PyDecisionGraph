from cpython.object cimport PyObject
from cpython.unicode cimport PyUnicode_AsUTF8, PyUnicode_FromString



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
