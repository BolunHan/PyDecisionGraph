"""The C-side evaluator: a parity case's "walked in C" arm.

A bake graph is normally walked by asking the root - ``root.eval()`` reaches
``c_dcg_root_node_eval`` through the wrapper and unpacks the leaf it landed on.
That is the C evaluator reached FROM PYTHON, and it is what a bake arm uses when
it wants the same door a caller would use.

This module is the other arrangement, and it is the one a host embedding the
layer has: nothing on the Python side walks anything. A caller hands the
evaluator a root WRAPPER, the evaluator runs the walk in C, and what comes back
is what the C layer produced - the outcome, how many nodes the walk crossed, and
the leaf it came to rest on, wrapped again so a Python reader can look at it. No
Python call happens between the root and the answer.

That difference is the whole point of a third arm: an evaluation that runs
entirely in C is the one that has to agree with the Python-driven walk, and when
the two disagree it is this seam - not the graph - that is wrong.

The root wrapper is reached through its public ``address``, exactly as the layer
itself reaches a manager: ``header`` is private to the class that owns it, so an
outside module names the block the way any other outside caller does.
"""

from libc.stdint cimport uintptr_t
from libcpp cimport bool as c_bool

from cpython.unicode cimport PyUnicode_FromString

from decision_graph.decision_tree.bake.c_hierarchy cimport RootLogicNode
from decision_graph.decision_tree.bake.c_node cimport (
    NODE_REGISTRY,
    c_dcg_root_node_eval,
    dcg_node,
    dcg_node_eval_path,
    dcg_root_node,
)
from decision_graph.decision_tree.bake.c_var cimport c_dcg_ret_code_name, c_dcg_var_pyunpack, dcg_ret_code


cdef class EvaluationOutcome:
    """What a C-side walk produced, read off the root's own record.

    Attributes:
        code: The ``DCG_ERR_*`` code the walk ended with.
        code_name: That code's stable name (``'OK'``, ``'UNBOUND'``, ...).
        n_nodes: How many nodes the walk crossed.
        failed: The wrapper of the node that refused, or None when none did.
        leaf: The wrapper of the node the walk came to rest on, or None when it
            never landed.
        value: The leaf's value, as a Python object - the same answer the
            Python-driven door hands back.
    """

    cdef readonly int    code
    cdef readonly str    code_name
    cdef readonly size_t n_nodes
    cdef readonly object failed
    cdef readonly object leaf
    cdef readonly object value

    def __cinit__(self, int code, str code_name, size_t n_nodes, object failed, object leaf, object value):
        self.code = code
        self.code_name = code_name
        self.n_nodes = n_nodes
        self.failed = failed
        self.leaf = leaf
        self.value = value

    def __repr__(self):
        landed = type(self.leaf).__name__ if self.leaf is not None else None
        return f'<EvaluationOutcome({self.code_name}, nodes={self.n_nodes}, leaf={landed})>'


cdef class GraphEvaluator:
    """Runs a baked graph in C, and reports what the C layer produced.

    The evaluator holds no graph: it is handed one per call, so the same object
    evaluates a root, a sub-graph and the next root without carrying state
    between them. What it does hold is the answer, in the form a caller can
    compare - see ``EvaluationOutcome``.
    """

    def evaluate(self, RootLogicNode root) -> EvaluationOutcome:
        """Walk the graph a root heads, in C, and report the walk.

        Args:
            root: The root of a baked graph. Nothing here bakes it - the pass is
                the caller's to have run, which is what makes the walk free of
                the checks the pass established.

        Returns:
            The outcome: the code, the nodes crossed, and the leaf and value the
            walk produced.

        Raises:
            RuntimeError: When the root has no block (a wrapper never built).
        """
        if root is None or not root.address:
            raise RuntimeError('<GraphEvaluator> needs a built RootLogicNode.')

        cdef dcg_root_node*     header = <dcg_root_node*> <uintptr_t> root.address
        cdef dcg_node_eval_path* path  = &header.eval_path

        cdef int ret_code = c_dcg_root_node_eval(header)

        cdef dcg_node* leaf   = path.leaf
        cdef dcg_node* failed = path.failed

        return EvaluationOutcome(
            ret_code,
            PyUnicode_FromString(c_dcg_ret_code_name(<dcg_ret_code> ret_code)),
            path.n_nodes,
            NODE_REGISTRY[<uintptr_t> failed] if failed else None,
            NODE_REGISTRY[<uintptr_t> leaf] if leaf else None,
            c_dcg_var_pyunpack(&leaf.out) if leaf else None,
        )
