"""The Cython half of the eval-hook tests.

An eval hook can be installed two ways, and the two are tested in different
places:

  - the PYTHON hook (``pre_eval``, ``eval_fn``, ``post_eval``) is a plain method,
    so a Python subclass overrides it and ``tests/test_bake_eval.py`` builds that
    subclass itself;
  - the CYTHON hook (``c_pre_eval``, ``c_eval_fn``, ``c_post_eval``) is a ``cdef``
    method, so only a Cython subclass can override it - and a Python test file
    cannot define one.

That second half is what this module exists for: action leaves that override a
Cython hook and nothing else, so the injection can be checked from the Python
suite without it needing a Cython test file of its own.
"""

from decision_graph.decision_tree.bake.c_action cimport NoAction
from decision_graph.decision_tree.bake.c_var cimport c_dcg_var_init_int, dcg_ret_code


cdef class HookedAction(NoAction):
    """An action leaf whose value is produced by a Cython eval hook."""

    cdef readonly list calls

    def __init__(self, **kwargs):
        self.calls = []
        NoAction.__init__(self, **kwargs)

    cdef int c_eval_fn(self):
        self.calls.append('c_eval_fn')
        return c_dcg_var_init_int(&self.header.out, 41)


cdef class RefusingAction(NoAction):
    """An action leaf that refuses in its Cython pre hook."""

    cdef readonly list calls

    def __init__(self, **kwargs):
        self.calls = []
        NoAction.__init__(self, **kwargs)

    cdef int c_pre_eval_fn(self):
        self.calls.append('c_pre_eval_fn')
        return dcg_ret_code.DCG_ERR_BUSY


cdef class WatchingAction(NoAction):
    """An action leaf that reads the slot in its Cython post hook.

    The eval hook puts a value there, so what the post hook reads is evidence of
    the order the stages ran in - and of the slot being the hook's to write.
    """

    cdef readonly list calls
    cdef readonly object seen

    def __init__(self, **kwargs):
        self.calls = []
        self.seen = None
        NoAction.__init__(self, **kwargs)

    cdef int c_eval_fn(self):
        self.calls.append('c_eval_fn')
        return c_dcg_var_init_int(&self.header.out, 11)

    cdef int c_post_eval_fn(self):
        self.calls.append('c_post_eval_fn')
        self.seen = self.header.out.value.as_int
        return dcg_ret_code.DCG_OK


cdef class BothHooksAction(NoAction):
    """Both halves of every hook on one node: the Cython one runs first."""

    cdef readonly list calls

    def __init__(self, **kwargs):
        self.calls = []
        NoAction.__init__(self, **kwargs)

    cdef int c_pre_eval_fn(self):
        self.calls.append('c_pre_eval_fn')
        return dcg_ret_code.DCG_OK

    def pre_eval_fn(self):
        self.calls.append('pre_eval_fn')

    cdef int c_eval_fn(self):
        self.calls.append('c_eval_fn')
        return c_dcg_var_init_int(&self.header.out, 3)

    def eval_fn(self):
        self.calls.append('eval_fn')

    cdef int c_post_eval_fn(self):
        self.calls.append('c_post_eval_fn')
        return dcg_ret_code.DCG_OK

    def post_eval_fn(self):
        self.calls.append('post_eval_fn')
