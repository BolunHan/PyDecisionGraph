"""Dump deep bake graphs to HTML, so the viewer can be read by eye.

Two graphs, each written twice — once as the build left it and once after the
bake — because that pair is the thing worth looking at:

  - the DEEP TREE is a plain single-root build: several stores, expressions
    over their reads, an else-only arm, and one branch a build reserved and
    never filled (a placeholder, which the bake consolidates into a no-action);
  - the SPLIT BUILD is the same graph assembled by two functions, the first
    stopping at a breakpoint and the second carrying on from it. The breakpoint
    is scaffolding: the un-baked page shows it standing where the branch
    stopped, and the baked page shows the continuation in its place.

Run it directly::

    ~/Projects/venv_313/bin/python tests/bake_webui_artifacts.py

Artifacts land in ``tests/artifacts/bake_webui/``.
"""

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from decision_graph.decision_tree.bake.c_action import (  # noqa: E402
    CancelAction, ClearAction, LongAction, NoAction, ShortAction,
)
from decision_graph.decision_tree.bake.c_collections import LogicMapping  # noqa: E402
from decision_graph.decision_tree.bake.c_const import ConstantNode  # noqa: E402
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator  # noqa: E402
from decision_graph.decision_tree.bake.c_hierarchy import BreakpointNode, RootLogicNode  # noqa: E402
from decision_graph.decision_tree.bake.c_logic_group import LogicGroup  # noqa: E402
from decision_graph.decision_tree.bake.c_node import LogicNode, PlaceholderNode  # noqa: E402
from decision_graph.webui import bake as webui_bake  # noqa: E402

ARTIFACT_DIR = pathlib.Path(__file__).resolve().parent / "artifacts" / "bake_webui"

_NAME_INDEX = [0]


def _unique(name: str) -> str:
    """A name nothing else in this process has taken.

    Both a store's name and a group's are registered process-wide, so a second
    run of the same build would be refused with a duplicate.
    """
    _NAME_INDEX[0] += 1
    return f'{name}{_NAME_INDEX[0]}'


def _store(name: str) -> LogicMapping:
    """A store whose name is unique per run."""
    return LogicMapping(name=_unique(name))


# ======================================================================
# The deep tree
# ======================================================================

def build_deep_tree() -> tuple[RootLogicNode, LogicMapping, LogicMapping]:
    """A decision graph seven levels down, over two stores.

    The reads are the stores' own AttrExpressions, so an expression is a NODE of
    the graph rather than a Python callable: its operands are not children, and
    the only place they show is the display text of the node that reads them.

    Every branch is written as a true arm and a false one, which is what a
    two-way node is: the statement after a ``with`` block fills the OTHER arm of
    the node that block opened, not another arm of the node above it.
    """
    portfolio = _store('portfolio')
    risk = _store('risk')

    with RootLogicNode(name='Deep Decision') as root:
        with portfolio:
            # A read binds to the store the build runs INSIDE, so each store's
            # reads are taken while that store is the one being run in. The two
            # contexts nest because that is what labels a node with both: the
            # graph below belongs to the portfolio and to the risk book at once.
            exposure = portfolio['exposure']
            working = portfolio['working_order']
            up = portfolio['up_prob']
            down = portfolio['down_prob']
            vol = portfolio['volatility']
            ttl = portfolio['ttl']

            with risk:
                limit = risk['limit']
                drift = risk['drift']

                with exposure > 0:
                    # Holding something: is an order still working?
                    with working > 0:
                        # Risk room left, and the market moving against us.
                        with (exposure < limit) & (down > 0.1):
                            # A break raised where the checks live, and carried
                            # on from immediately: the breakpoint stands on the
                            # arm until the bake takes it down.
                            with LogicGroup(name=_unique('checks')) as checks:
                                with vol > 0.25:
                                    ShortAction()
                                    breakpoint = BreakpointNode.break_(break_from=checks)
                            with breakpoint:
                                with drift > 0.0:
                                    LongAction()
                                    ClearAction()
                        # No room: unwind, but only if the position is fresh.
                        with ttl > 30:
                            CancelAction()
                            ClearAction()
                    # Nothing working: decide on the signal alone.
                    with up > 0.5:
                        with BinaryExpression(ExpressionOperator.add, up, down) > 0.9:
                            LongAction()
                            NoAction()
                        ShortAction()

    return root, portfolio, risk


# ======================================================================
# The split build
# ======================================================================

def build_first_half(root: RootLogicNode, book: LogicMapping) -> BreakpointNode:
    """Build up to the break, and hand the breakpoint back.

    What a build does when it stops in one function and carries on in another:
    the group is left, the breakpoint it raised is left waiting, and the next
    function takes it up. Nothing is carried between them but the root.
    """
    with root:
        with book:
            # The read is taken while the store is the group being run in: the
            # group below carries no entries, so a read created inside it would
            # have no store to read from.
            exposure = book['exposure']
            with LogicGroup(name=_unique('checks')) as checks:
                with exposure > 0:
                    BreakpointNode.break_(break_from=checks)
    return root.get_breakpoint()


def build_second_half(breakpoint: BreakpointNode, book: LogicMapping) -> None:
    """Carry the branch on from where the first half stopped.

    Entering the breakpoint is what resuming means: it opens one arm, and what
    is built inside becomes the node it resumed into.
    """
    with breakpoint:
        with book:
            up = book['up_prob']
            with up > 0.5:
                LongAction()
                ShortAction()


def build_split_tree() -> tuple[RootLogicNode, BreakpointNode, LogicMapping]:
    book = _store('book')
    root = RootLogicNode(name='Split Build')
    breakpoint = build_first_half(root, book)
    build_second_half(breakpoint, book)
    return root, breakpoint, book


# ======================================================================
# Feeding and dumping
# ======================================================================

def feed_deep_tree(portfolio: LogicMapping, risk: LogicMapping, live: bool) -> None:
    portfolio['exposure'] = 2 if live else 0
    portfolio['working_order'] = 1
    portfolio['up_prob'] = 0.85
    portfolio['down_prob'] = 0.15
    portfolio['volatility'] = 0.3
    portfolio['ttl'] = 45.0
    risk['limit'] = 10
    risk['drift'] = 0.05


def dump(name: str, node: LogicNode, with_eval: bool) -> pathlib.Path:
    ARTIFACT_DIR.mkdir(parents=True, exist_ok=True)
    target = ARTIFACT_DIR / name
    webui_bake.to_html(node, str(target), with_eval=with_eval)
    return target


def main() -> int:
    written = []

    # --- The deep tree, as the build left it: scaffolding still in place. ---
    root, portfolio, risk = build_deep_tree()
    feed_deep_tree(portfolio, risk, live=True)
    written.append(dump('01_deep_tree_unbaked.html', root, with_eval=False))

    # --- The same graph, baked: the stand-in is consolidated away. ---
    report = root.bake()
    written.append(dump('02_deep_tree_baked.html', root, with_eval=False))

    # --- And baked again, this time walked, so the path is drawn. ---
    decision = root()
    written.append(dump('03_deep_tree_baked_walked.html', root, with_eval=True))

    # --- The split build, un-baked: the breakpoint is still standing. ---
    split_root, breakpoint, book = build_split_tree()
    book['exposure'] = 2
    book['volatility'] = 0.3
    book['up_prob'] = 0.6
    written.append(dump('04_split_build_unbaked.html', split_root, with_eval=False))

    # --- The split build, baked: the continuation took the break's place. ---
    split_report = split_root.bake()
    split_root()
    written.append(dump('05_split_build_baked.html', split_root, with_eval=True))

    print(f'deep tree  : bake {report.code_name} · {report.nodes} nodes · depth {report.depth} '
          f'· sealed {report.sealed} · locked {report.locked} · decision {decision!r}')
    print(f'split build: bake {split_report.code_name} · {split_report.nodes} nodes · depth {split_report.depth}')
    for path in written:
        print(f'  {path.relative_to(ARTIFACT_DIR.parent.parent)}  ({path.stat().st_size:,} bytes)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
