from cpython.object cimport PyObject
from cpython.unicode cimport PyUnicode_AsUTF8


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
