"""A tree assembled by two functions, through a breakpoint.

A build can stop in one place and be carried on in another: the first function
opens the root, raises a break where the branch should stop, and leaves; the
breakpoint it left is what the second function enters to carry the branch on.
Nothing crosses between the two but the root — which is how a large graph is
assembled from small, separately testable pieces.

The example uses the bake layer, where this flow is exercised by the test
suite: the breakpoint is ''scaffolding'', and ``bake()`` is what takes it down
once the graph is complete — the continuation takes the break's place.

Run it with the package installed, from any directory::

    python split_build.py
"""

from decision_graph.decision_tree.bake.c_action import LongAction, ShortAction
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_hierarchy import BreakpointNode, RootLogicNode
from decision_graph.decision_tree.bake.c_logic_group import LogicGroup

state = {
    "exposure": 2,
    "up_prob": 0.6,
}

book = LogicMapping(name='book')


def build_first_half(root, book):
    """Build up to the break and hand the breakpoint back."""
    with root:
        with book:
            # The read is taken while the store is the group being run in; a
            # read built inside a group that holds no entries has no store.
            exposure = book['exposure']

            with LogicGroup(name='checks') as checks:
                with exposure > 0:
                    BreakpointNode.break_(break_from=checks)

    return root.get_breakpoint()


def build_second_half(breakpoint, book):
    """Carry the branch on from where the first half stopped."""
    with breakpoint:
        with book:
            up = book['up_prob']

            with up > 0.5:
                LongAction()
                ShortAction()


root = RootLogicNode(name='Split Build')

# Two functions, two scopes: the first builds to the break, the second resumes.
breakpoint_node = build_first_half(root, book)
build_second_half(breakpoint_node, book)

# Feed the store, bake the graph. The breakpoint is scaffolding: the bake takes
# it down, and the continuation takes its place on the arm.
book['exposure'] = state['exposure']
book['up_prob'] = state['up_prob']
report = root.bake()
print(f'bake: {report.code_name} · {report.nodes} nodes · {report.depth} levels deep')

print(f'decision: {root()}')
print('walk:', ' > '.join(str(node) for node in root.eval_path))
