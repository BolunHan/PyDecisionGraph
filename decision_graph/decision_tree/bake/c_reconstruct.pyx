from libc.stdio cimport fflush, fprintf, stderr

from .c_const cimport dcg_variable_node
from .c_edge cimport EDGE_REGISTRY, NodeEdgeCondition
from .c_expr cimport dcg_expression_node
from .c_hierarchy cimport dcg_breakpoint_node
from .c_logic_group cimport GROUP_REGISTRY, dcg_logic_group
from .c_node cimport NODE_REGISTRY, c_dcg_node_root, dcg_node_type
from .c_var cimport c_dcg_var_pyunpack


# ========== The Variants ==========

cdef ConstantNode c_dcg_node_reconstruct_const(dcg_node* header, bint owner=False):
    cdef ConstantNode node = ConstantNode.__new__(ConstantNode)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    return node


cdef VariableNode c_dcg_node_reconstruct_variable(dcg_node* header, bint owner=False):
    cdef VariableNode node = VariableNode.__new__(VariableNode)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    return node


cdef AttrExpression c_dcg_node_reconstruct_attr(dcg_node* header, bint owner=False):
    cdef AttrExpression   node  = AttrExpression.__new__(AttrExpression)
    cdef dcg_logic_group* group = (<dcg_variable_node*> header).logic_group

    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    if group:
        node.logic_group = <object> GROUP_REGISTRY[<uintptr_t> group]
    return node


cdef void c_dcg_node_restore_operands(ExpressionNode node, dcg_expression_node* header):
    cdef size_t     i
    cdef dcg_node** operands = header.components
    cdef object     value    = None

    node.c_components_alloc(header.n_args)
    for i in range(header.n_args):
        if operands and operands[i]:
            node.c_components_assign(i, c_dcg_node_reconstruct(operands[i], False))
            continue

        value = c_dcg_var_pyunpack(&header.args[i])
        if value is not None:
            node.c_components_assign(i, ConstantNode(value))


cdef ExpressionNode c_dcg_node_reconstruct_expr(dcg_node* header, bint owner=False):
    cdef ExpressionNode node = ExpressionNode.__new__(ExpressionNode)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    c_dcg_node_restore_operands(node, <dcg_expression_node*> header)
    return node


cdef UnaryExpression c_dcg_node_reconstruct_unary(dcg_node* header, bint owner=False):
    cdef UnaryExpression node = UnaryExpression.__new__(UnaryExpression)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    c_dcg_node_restore_operands(node, <dcg_expression_node*> header)
    return node


cdef BinaryExpression c_dcg_node_reconstruct_binary(dcg_node* header, bint owner=False):
    cdef BinaryExpression node = BinaryExpression.__new__(BinaryExpression)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    c_dcg_node_restore_operands(node, <dcg_expression_node*> header)
    return node


cdef TernaryExpression c_dcg_node_reconstruct_ternary(dcg_node* header, bint owner=False):
    cdef TernaryExpression node = TernaryExpression.__new__(TernaryExpression)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    c_dcg_node_restore_operands(node, <dcg_expression_node*> header)
    return node


cdef ActionNode c_dcg_node_reconstruct_action(dcg_node* header, bint owner=False):
    cdef ActionNode node = ActionNode.__new__(ActionNode)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    return node


cdef NoAction c_dcg_node_reconstruct_noaction(dcg_node* header, bint owner=False):
    cdef NoAction node = NoAction.__new__(NoAction)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    return node


cdef LongAction c_dcg_node_reconstruct_longaction(dcg_node* header, bint owner=False):
    cdef LongAction node = LongAction.__new__(LongAction)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    return node


cdef ShortAction c_dcg_node_reconstruct_shortaction(dcg_node* header, bint owner=False):
    cdef ShortAction node = ShortAction.__new__(ShortAction)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    return node


cdef CancelAction c_dcg_node_reconstruct_cancelaction(dcg_node* header, bint owner=False):
    cdef CancelAction node = CancelAction.__new__(CancelAction)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    return node


cdef ClearAction c_dcg_node_reconstruct_clearaction(dcg_node* header, bint owner=False):
    cdef ClearAction node = ClearAction.__new__(ClearAction)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    return node


cdef PlaceholderNode c_dcg_node_reconstruct_placeholder(dcg_node* header, bint owner=False):
    cdef PlaceholderNode node = PlaceholderNode.__new__(PlaceholderNode)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    return node


cdef RootLogicNode c_dcg_node_reconstruct_root(dcg_node* header, bint owner=False):
    cdef RootLogicNode node = RootLogicNode.__new__(RootLogicNode)
    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    return node


cdef BreakpointNode c_dcg_node_reconstruct_breakpoint(dcg_node* header, bint owner=False):
    cdef BreakpointNode   node  = BreakpointNode.__new__(BreakpointNode)
    cdef dcg_logic_group* group = (<dcg_breakpoint_node*> header).break_from

    c_dcg_node_attach_header(node, header, owner)
    node.c_register_node()
    if group:
        node.break_from = <object> GROUP_REGISTRY[<uintptr_t> group]
    return node


# ========== The Walk ==========

cdef void c_dcg_node_restore_edges(LogicNode node, dcg_node* header):
    cdef dcg_node*         child = header.children
    cdef LogicNode         child_node
    cdef NodeEdgeCondition condition

    while child:
        condition  = <NodeEdgeCondition> EDGE_REGISTRY[<uintptr_t> child.condition_to_parent]
        child_node = c_dcg_node_reconstruct(child, False)

        node.children[condition]       = child_node
        child_node.parent              = node
        child_node.condition_to_parent = condition

        child = child.next_sibling

    if header.parent:
        node.parent = c_dcg_node_reconstruct(header.parent, False)


cdef LogicNode c_dcg_node_reconstruct(dcg_node* header, bint owner=False):
    if not header:
        raise ValueError('A NULL header is not a node: there is nothing to reconstruct.')

    cdef uintptr_t address = <uintptr_t> header
    if address in NODE_REGISTRY:
        return NODE_REGISTRY[address]

    cdef dcg_node_type ntype = header.ntype
    cdef LogicNode     node

    # The input family: the literals, then the read.
    if ntype == dcg_node_type.DCG_NODE_INPUT or ntype == dcg_node_type.DCG_NODE_TRUE or ntype == dcg_node_type.DCG_NODE_FALSE or ntype == dcg_node_type.DCG_NODE_DOUBLE or ntype == dcg_node_type.DCG_NODE_STRING or ntype == dcg_node_type.DCG_NODE_INT:
        node = c_dcg_node_reconstruct_const(header, owner)
    elif ntype == dcg_node_type.DCG_NODE_VARIABLE:
        # A read names the group it reads and a plain variable names none, so
        # the node's own state is what tells the two classes apart - the type
        # they share cannot.
        node = c_dcg_node_reconstruct_attr(header, owner) if (<dcg_variable_node*> header).logic_group else c_dcg_node_reconstruct_variable(header, owner)
    # The operator family, by arity.
    elif ntype == dcg_node_type.DCG_NODE_OP:
        node = c_dcg_node_reconstruct_expr(header, owner)
    elif ntype == dcg_node_type.DCG_NODE_UNARY:
        node = c_dcg_node_reconstruct_unary(header, owner)
    elif ntype == dcg_node_type.DCG_NODE_BINARY:
        node = c_dcg_node_reconstruct_binary(header, owner)
    elif ntype == dcg_node_type.DCG_NODE_TERNARY:
        node = c_dcg_node_reconstruct_ternary(header, owner)
    # The action family, by kind.
    elif ntype == dcg_node_type.DCG_NODE_ACTION:
        node = c_dcg_node_reconstruct_action(header, owner)
    elif ntype == dcg_node_type.DCG_NODE_NOACTION:
        node = c_dcg_node_reconstruct_noaction(header, owner)
    elif ntype == dcg_node_type.DCG_NODE_LONGACTION:
        node = c_dcg_node_reconstruct_longaction(header, owner)
    elif ntype == dcg_node_type.DCG_NODE_SHORTACTION:
        node = c_dcg_node_reconstruct_shortaction(header, owner)
    elif ntype == dcg_node_type.DCG_NODE_CANCELACTION:
        node = c_dcg_node_reconstruct_cancelaction(header, owner)
    elif ntype == dcg_node_type.DCG_NODE_CLEARACTION:
        node = c_dcg_node_reconstruct_clearaction(header, owner)
    elif ntype == dcg_node_type.DCG_NODE_PLACEHOLDER:
        node = c_dcg_node_reconstruct_placeholder(header, owner)
    # The special family.
    elif ntype == dcg_node_type.DCG_NODE_ROOT:
        node = c_dcg_node_reconstruct_root(header, owner)
    elif ntype == dcg_node_type.DCG_NODE_BREAKPOINT:
        node = c_dcg_node_reconstruct_breakpoint(header, owner)
    else:
        # Nothing claims the type, and the difference matters. Two cases reach
        # here with a reason, and both are answered by the generic view: a family
        # head with no variant of its own (DCG_NODE_SPECIAL), and a CALL, whose
        # callee is composed into the node's display text and stored nowhere - a
        # rebuilt call node would not know what it calls. A variant nothing
        # claims is a type this module was never taught, and that is reported
        # rather than silently wrapped as something it is not.
        if ntype != dcg_node_type.DCG_NODE_SPECIAL and ntype != dcg_node_type.DCG_NODE_CALL:
            fprintf(
                stderr, "[DCG] c_dcg_node_reconstruct: node type 0x%x has no reconstruction variant - wrapping it as the base node\n", <unsigned int> ntype
            )
            fflush(stderr)
        node = LogicNode.c_from_header(header, owner)
        node.c_register_node()

    c_dcg_node_restore_edges(node, header)
    return node


# ========== The Proxies ==========

cpdef LogicNode c_dcg_node_reconstruct_from_address(uintptr_t address, bint owner=False):
    return c_dcg_node_reconstruct(<dcg_node*> address, owner)


cpdef LogicNode c_dcg_node_root_from_address(uintptr_t address, bint owner=False):
    return c_dcg_node_reconstruct(c_dcg_node_root(<dcg_node*> address), owner)
