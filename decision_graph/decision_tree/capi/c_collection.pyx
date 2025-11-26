from collections.abc import Mapping, Sequence, Generator

from cpython.dict cimport PyDict_GetItem
from cpython.object cimport PyObject
from cpython.ref cimport Py_INCREF

from .c_abc cimport LogicGroup
from .c_node cimport AttrExpression, GetterExpression

from . import LOGGER


cdef class LogicMapping(LogicGroup):
    def __cinit__(self, *, str name=None, object data=None, LogicGroup parent=None, dict contexts=None, **kwargs):
        cdef object ctx_data
        if data is None:
            ctx_data = self.contexts.setdefault('data', {})
            if isinstance(ctx_data, dict):
                self.data = <dict> ctx_data
            elif isinstance(ctx_data, Mapping):
                LOGGER.info(f'Using non-dict mapping for {self} data, unlinking and converting to dict...')
                self.data = dict(ctx_data)
            else:
                raise TypeError("The 'data' parameter must be a Mapping!.")
        else:
            self.data = <dict> data

    cdef object c_get(self, str key):
        cdef PyObject* v = PyDict_GetItem(self.data, key)
        if v:
            Py_INCREF(<object> v)
            return <object> v
        raise KeyError(key)

    def __bool__(self):
        return bool(self.data)

    def __len__(self):
        return self.data.__len__()

    def __getitem__(self, str key):
        return AttrExpression(attr=key, logic_group=self)

    def __getattr__(self, str key):
        return AttrExpression(attr=key, logic_group=self)

    def __contains__(self, str key):
        return key in self.data

    def update(self, *args, **kwargs):
        self.data.update(*args, **kwargs)

    def clear(self):
        self.data.clear()


cdef class LogicSequence(LogicGroup):
    def __cinit__(self, *, str name=None, object data=None, LogicGroup parent=None, dict contexts=None, **kwargs):
        cdef object ctx_data
        if data is None:
            ctx_data = self.contexts.setdefault('data', [])
            if isinstance(ctx_data, list):
                self.data = <list> ctx_data
            elif isinstance(ctx_data, Sequence):
                LOGGER.info(f'Using non-list sequence for {self} data, converting to list...')
                self.data = list(ctx_data)
            else:
                raise TypeError("The 'data' parameter must be a Sequence!.")
        else:
            self.data = <list> data

    cdef object c_get(self, ssize_t index):
        return self.data[index]

    # === Python interface conveniences ===

    def __iter__(self):
        for index, _ in enumerate(self.data):
            yield GetterExpression(key=index, logic_group=self)

    def __len__(self):
        return len(self.data)

    def __getitem__(self, index):
        return GetterExpression(key=index, logic_group=self)

    def __contains__(self, item):
        return item in self.data

    def append(self, value):
        self.data.append(value)

    def extend(self, iterable):
        self.data.extend(iterable)

    def insert(self, index, value):
        self.data.insert(index, value)

    def remove(self, value):
        self.data.remove(value)

    def pop(self, index=-1):
        return self.data.pop(index)

    def clear(self):
        self.data.clear()

    def __bool__(self):
        return bool(self.data)


cdef class LogicGenerator(LogicGroup):
    def __cinit__(self, *, str name=None, object data=None, LogicGroup parent=None, dict contexts=None, **kwargs):
        if data is None:
            data = self.contexts.setdefault('data')

        if isinstance(data, Generator):
            self.data = data
        raise TypeError("The 'data' parameter must be a Generator!.")

    cdef object c_next(self):
        return next(self.data)

    # === Python generator protocol & control methods (added) ===
    def __iter__(self):
        return self

    def __next__(self):
        return self.c_next()

    def send(self, value):
        return self.data.send(value)

    def throw(self, typ, val=None, tb=None):
        return self.data.throw(typ, val, tb)

    def close(self):
        return self.data.close()
