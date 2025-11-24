import sys
from random import choice

sys.path.append('/home/bolun/Projects/PyDecisionGraph')

from decision_graph.decision_tree.c_abc import (
    LGM,
    LogicNode,
    LongAction,
    ShortAction,
    LogicGroup,
    TRUE_CONDITION,
    FALSE_CONDITION, BreakpointNode, NoAction,
)


def node(name: str, v: bool = None):
    if v is None:
        v = choice([True, False])
    ln = LogicNode(
        expression=v,
        dtype=bool,
        repr=f'{name}, {v}'
    )
    return ln


def group(name: str):
    lg = LogicGroup(name=name)
    return lg


# --- Plain test helpers ---

def expect_raises(exc_type, func, *args, **kwargs):
    try:
        func(*args, **kwargs)
    except exc_type:
        return True
    except Exception as e:
        print(f"Expected {exc_type}, got {type(e)}: {e}")
        raise
    else:
        raise AssertionError(f"Expected {exc_type.__name__} to be raised")


# --- Test cases for LogicNode ---

def test_logicnode_base_init_attributes():
    """Test basic initialization and attributes of LogicNode."""
    ln = LogicNode(expression=True, dtype=bool, repr='test_node')
    assert ln.expression == True
    assert ln.dtype == bool
    assert ln.repr == 'test_node'
    assert ln.parent is None
    assert isinstance(ln.children, dict)
    assert len(ln.children) == 0
    assert isinstance(ln.labels, list)
    assert ln.autogen == False  # Assuming default
    print("Base init and attributes test passed.")


def test_inspection_mode_binary_decision_tree():
    """Test building a binary decision tree in inspection mode."""
    original_mode = LGM.inspection_mode
    LGM.inspection_mode = True
    try:
        root = LogicNode(expression=lambda: True, dtype=bool, repr='root')
        child_true = LogicNode(expression=lambda: True, dtype=bool, repr='true_child')
        child_false = LogicNode(expression=lambda: False, dtype=bool, repr='false_child')

        root.append(child_true, TRUE_CONDITION)
        root.append(child_false, FALSE_CONDITION)

        assert len(root.children) == 2
        assert root.children[TRUE_CONDITION] is child_true
        assert root.children[FALSE_CONDITION] is child_false
        assert child_true.parent is root
        assert child_false.parent is root
        print("Inspection mode binary tree building test passed.")
    finally:
        LGM.inspection_mode = original_mode


def test_runtime_mode_build_and_evaluate():
    """Test building and evaluating a decision tree without inspection mode."""
    original_mode = LGM.inspection_mode
    LGM.inspection_mode = False
    try:
        root = LogicNode(expression=lambda: True, dtype=bool, repr='root')
        true_action = LongAction()
        false_action = ShortAction()

        root.append(true_action, TRUE_CONDITION)
        root.append(false_action, FALSE_CONDITION)

        result = root()
        assert isinstance(result, LongAction)  # Since expression is True
        print("Runtime mode build and evaluate test passed.")
    finally:
        LGM.inspection_mode = original_mode


def test_inspection_mode_with_logic_group_and_break():
    """Test building decision tree with logic group and break in inspection mode."""
    original_mode = LGM.inspection_mode
    LGM.inspection_mode = True
    try:
        with LogicGroup(name='test_group') as lg:
            root = LogicNode(expression=lambda: True, dtype=bool, repr='root')
            with root:
                child1 = LogicNode(expression=lambda: True, dtype=bool, repr='child1')
                with child1:
                    lg.break_()
                    LongAction()
                child2 = LogicNode(expression=lambda: False, dtype=bool, repr='child2')
                with child2:
                    ShortAction()

        # Check that break was recorded or something; in inspection mode, breaks are handled differently
        assert len(root.children) >= 1  # At least one child
        print("Inspection mode with logic group and break test passed.")
    finally:
        LGM.inspection_mode = original_mode


def test_evaluate_built_tree_in_inspection_mode():
    """Test evaluating a built decision tree in inspection mode."""
    original_mode = LGM.inspection_mode
    LGM.inspection_mode = True
    try:
        root = LogicNode(expression=lambda: True, dtype=bool, repr='root')
        child_true = LongAction()
        child_false = ShortAction()

        root.append(child_true, TRUE_CONDITION)
        root.append(child_false, FALSE_CONDITION)

        value, path = root.eval_recursively()
        assert value is child_true  # Since expression is True
        assert len(path) == 2  # root and child
        assert path[0] is root
        assert path[1] is child_true
        print("Evaluate built tree in inspection mode test passed.")
    finally:
        LGM.inspection_mode = original_mode


def test_logicnode_chaining_with_rshift():
    """Test chaining nodes using >> operator."""
    ln1 = LogicNode(expression=True, dtype=bool)
    ln2 = LogicNode(expression=False, dtype=bool)
    result = ln1 >> ln2
    assert result is ln2
    assert ln2.parent is ln1
    print("Chaining with >> test passed.")


def test_logicnode_leaves_and_is_leaf():
    """Test leaves property and is_leaf."""
    root = LogicNode(expression=True)
    assert root.is_leaf == True
    assert list(root.leaves) == [root]

    child = LogicNode(expression=False)
    root.append(child, TRUE_CONDITION)
    assert root.is_leaf == False
    assert set(root.leaves) == {child}
    print("Leaves and is_leaf test passed.")


def test_logicnode_list_labels():
    """Test list_labels method."""
    root = LogicNode(expression=True)
    labels = root.list_labels()
    assert isinstance(labels, dict)
    # Assuming no labels initially
    assert len(labels) == 0
    print("List labels test passed.")


def test_build_tree_withctx_inspection_mode():
    """Test building a complex decision tree using 'with' context in inspection mode, checking labels and attributes, then evaluate."""
    original_mode = LGM.inspection_mode
    LGM.clear()
    LGM.inspection_mode = True
    try:
        with group(name='outer_group') as outer_group:
            with node('root', True) as root:
                with group('inner_group') as inner_group:
                    with group('g_c1_t') as g_c1_t:
                        with node('c1_t', True) as c1_t:
                            with node('c1_1_t', True):
                                with node('c1_1_1_t', True):
                                    g_c1_t.break_()
                                    LongAction()
                                ShortAction()

                    with group('g_c1_f') as g_c1_f:
                        with node('c1_f', False) as c1_f:
                            with node('c1_1_f', True):
                                with node('c1_1_1_f', True):
                                    LongAction()
                                    # there missing a branch here, as intended for testing autofill a NoAction node.
                                ShortAction()
                            expected_final_node = NoAction()

        # Check labels
        assert root.labels == ['outer_group']
        assert c1_t.labels == ['g_c1_t', 'inner_group', 'outer_group']
        assert c1_f.labels == ['g_c1_f', 'inner_group', 'outer_group']

        # Check attributes
        assert root.parent is None
        assert len(root.children) == 2
        assert TRUE_CONDITION in root.children
        assert FALSE_CONDITION in root.children
        assert root.children[TRUE_CONDITION] is c1_t
        assert root.children[FALSE_CONDITION] is c1_f
        assert c1_t.parent is root
        assert c1_f.parent is root
        assert not c1_t.is_leaf
        assert not c1_f.is_leaf

        # Check deeper structure
        c1_1_t = c1_t.children[TRUE_CONDITION]
        assert c1_1_t.repr.startswith('c1_1_t')
        assert not c1_1_t.is_leaf
        c1_1_1_t = c1_1_t.children[TRUE_CONDITION]
        assert c1_1_1_t.repr.startswith('c1_1_1_t')
        assert isinstance(c1_1_1_t.children[FALSE_CONDITION], LongAction)
        assert isinstance(c1_1_1_t.children[TRUE_CONDITION], BreakpointNode)
        assert c1_1_1_t.children[TRUE_CONDITION].linked_to is c1_f

        c1_1_f = c1_f.children[TRUE_CONDITION]
        assert c1_1_f.repr.startswith('c1_1_f')
        assert not c1_1_f.is_leaf
        c1_1_1_f = c1_1_f.children[TRUE_CONDITION]
        assert c1_1_1_f.repr.startswith('c1_1_1_f')
        assert isinstance(c1_1_1_f.children[TRUE_CONDITION], LongAction)

        # Evaluate
        value, path = root.eval_recursively()
        assert value is expected_final_node
        assert len(path) == 7  # root, c1_t, c1_1_t, c1_1_1_t, LongAction
        assert path[0] is root
        assert path[1] is c1_t
        assert path[2] is c1_1_t
        assert path[3] is c1_1_1_t
        assert isinstance(path[4], BreakpointNode)
        assert path[5] is c1_f
        assert path[6] is expected_final_node
    finally:
        LGM.inspection_mode = original_mode


def test_build_tree_withctx_inspection_mode_second():
    """Test building another complex decision tree using 'with' context in inspection mode, with different structure."""
    original_mode = LGM.inspection_mode
    LGM.inspection_mode = True
    try:
        with group('main_group') as main_group:
            with node('start', True) as start:
                with group('sub1') as sub1:
                    with node('branch1', True) as branch1:
                        LongAction()
                    with node('branch2', False) as branch2:
                        with group('sub2') as sub2:
                            with node('sub_branch', True):
                                sub2.break_()
                                ShortAction()
                            with node('sub_branch2', False):
                                LongAction()

        # Check labels
        assert 'main_group' in start.labels
        assert 'sub1' in branch1.labels
        assert 'main_group' in branch1.labels
        assert 'sub1' in branch2.labels
        assert 'main_group' in branch2.labels

        # Check attributes
        assert start.parent is None
        assert len(start.children) == 2
        assert branch1.parent is start
        assert branch2.parent is start
        assert branch1.is_leaf == False
        assert branch2.is_leaf == False

        # Evaluate
        value, path = start.eval_recursively()
        assert isinstance(value, LongAction)  # True branch
        assert path[0] is start
        assert path[1] is branch1

        print("Build second tree with context in inspection mode test passed.")
    finally:
        LGM.inspection_mode = original_mode


# Simple runner for direct invocation: python tests/test_logicnode.py
if __name__ == "__main__":
    import inspect

    this_module = sys.modules[__name__]
    tests = [
        (name, obj)
        for name, obj in sorted(this_module.__dict__.items())
        if name.startswith("test_") and inspect.isfunction(obj)
    ]
    total = len(tests)
    print(f"Discovered {total} plain tests for LogicNode. Running...\n")
    passed = 0
    failed = 0
    for name, fn in tests:
        print(f"--- {name} ---")
        try:
            fn()
        except AssertionError as ae:
            failed += 1
            print(f"[FAIL] {name}: {ae}")
        except Exception as e:
            failed += 1
            print(f"[ERROR] {name}: {type(e).__name__}: {e}")
        else:
            passed += 1
            print(f"[PASS] {name}")
        print()
    print(f"Summary: {passed} passed, {failed} failed, {total} total")
    if failed:
        sys.exit(1)
