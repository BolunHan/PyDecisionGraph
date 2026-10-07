"""Smoke tests over a capi ``LogicNode`` tree, built and walked in inspection mode.

Two builds, asserted rather than printed: a plain nested tree whose display is
its root's own repr, and a store-scoped tree whose walk stops at a breakpoint
and resumes into the sibling that follows it. The walk records what it entered
but NOT the node it was asked on — ``eval_recursively`` omits its subject, which
is this layer's own semantics and is pinned here so a change to it is visible.
"""

from decision_graph.decision_tree.capi.c_abc import (
    LGM,
    LogicGroup,
    LogicNode,
    LongAction,
    NoAction,
    ShortAction,
)


def node(name: str, v: bool = False) -> LogicNode:
    """One boolean node with a readable display text."""
    return LogicNode(
        expression=v,
        dtype=bool,
        repr=f'{name}, {v}'
    )


def test_the_plain_tree_shows_its_root_and_walks_to_no_action():
    """A nested build with no store: the root's repr, its two arms, and the walk into an empty one."""
    original_mode = LGM.inspection_mode
    LGM.inspection_mode = True
    try:
        with node('ln_root', False) as root:
            with node('ln_child_1', True):
                with node('ln_child_1_1', True):
                    LongAction()
                with node('ln_child_1_2', False):
                    NoAction()
                    ShortAction()
            with node('ln_child_2', False):
                pass

        assert str(root) == "<LogicNode>('ln_root, False')"

        arms = [(str(condition), str(child)) for condition, child in root.children.items()]
        assert arms == [
            ('False', "<LogicNode>('ln_child_2, False')"),
            ('True', "<LogicNode>('ln_child_1, True')"),
        ]

        value, path = root.eval_recursively()
        assert str(value) == '<NoAction>(sig=0)'
        assert [str(n) for n in path] == [
            "<LogicNode>('ln_child_2, False')",
            '<NoAction>(sig=0)',
        ]
    finally:
        LGM.inspection_mode = original_mode


def test_the_group_scoped_tree_walks_through_the_breakpoint():
    """A break halts the walk where it stands; the path crosses the breakpoint and resumes at the sibling."""
    original_mode = LGM.inspection_mode
    LGM.inspection_mode = True
    try:
        with LogicGroup(name='ln_root_group'):
            with node('ln_root', True) as root:
                with LogicGroup(name='ln_group_1') as group_1:
                    with node('ln_child_1', True):
                        with node('ln_child_1_1', True):
                            group_1.break_()
                            LongAction()
                with node('ln_child_2', False):
                    NoAction()
                    ShortAction()

        value, path = root.eval_recursively()
        assert str(value) == '<ShortAction>(sig=-1)'
        assert [str(n) for n in path] == [
            "<LogicNode>('ln_child_1, True')",
            "<LogicNode>('ln_child_1_1, True')",
            "<BreakpointNode connected>(break_from=<LogicGroup>('ln_group_1'))",
            "<LogicNode>('ln_child_2, False')",
            '<ShortAction>(sig=-1)',
        ]
    finally:
        LGM.inspection_mode = original_mode
