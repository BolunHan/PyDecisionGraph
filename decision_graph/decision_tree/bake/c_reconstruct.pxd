from libc.stdint cimport uintptr_t

from .c_action cimport ActionNode, CancelAction, ClearAction, LongAction, NoAction, ShortAction
from .c_collections cimport AttrExpression
from .c_const cimport ConstantNode, VariableNode
from .c_expr cimport BinaryExpression, ExpressionNode, TernaryExpression, UnaryExpression
from .c_hierarchy cimport BreakpointNode, RootLogicNode
from .c_node cimport LogicNode, NODE_REGISTRY, PlaceholderNode, dcg_node


cdef inline void c_dcg_node_attach_header(LogicNode node, dcg_node* header, bint owner):
    cdef uintptr_t parent_address = 0

    node.header = header
    node.owner = owner
    node.parent = None
    if header.parent:
        parent_address = <uintptr_t> header.parent
        if parent_address in NODE_REGISTRY:
            node.parent = NODE_REGISTRY[parent_address]


# === Variant Reconstruct ===

cdef ConstantNode c_dcg_node_reconstruct_const(dcg_node* header, bint owner=?)

cdef VariableNode c_dcg_node_reconstruct_variable(dcg_node* header, bint owner=?)

cdef AttrExpression c_dcg_node_reconstruct_attr(dcg_node* header, bint owner=?)

cdef ExpressionNode c_dcg_node_reconstruct_expr(dcg_node* header, bint owner=?)

cdef UnaryExpression c_dcg_node_reconstruct_unary(dcg_node* header, bint owner=?)

cdef BinaryExpression c_dcg_node_reconstruct_binary(dcg_node* header, bint owner=?)

cdef TernaryExpression c_dcg_node_reconstruct_ternary(dcg_node* header, bint owner=?)

cdef ActionNode c_dcg_node_reconstruct_action(dcg_node* header, bint owner=?)

cdef NoAction c_dcg_node_reconstruct_noaction(dcg_node* header, bint owner=?)

cdef LongAction c_dcg_node_reconstruct_longaction(dcg_node* header, bint owner=?)

cdef ShortAction c_dcg_node_reconstruct_shortaction(dcg_node* header, bint owner=?)

cdef CancelAction c_dcg_node_reconstruct_cancelaction(dcg_node* header, bint owner=?)

cdef ClearAction c_dcg_node_reconstruct_clearaction(dcg_node* header, bint owner=?)

cdef PlaceholderNode c_dcg_node_reconstruct_placeholder(dcg_node* header, bint owner=?)

cdef RootLogicNode c_dcg_node_reconstruct_root(dcg_node* header, bint owner=?)

cdef BreakpointNode c_dcg_node_reconstruct_breakpoint(dcg_node* header, bint owner=?)

# === Dispatch ===

cdef LogicNode c_dcg_node_reconstruct(dcg_node* header, bint owner=?)

cpdef LogicNode c_dcg_node_reconstruct_from_address(uintptr_t address, bint owner=?)

cpdef LogicNode c_dcg_node_root_from_address(uintptr_t address, bint owner=?)
