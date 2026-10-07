"""Smoke tests over the capi web viewer's doors, on a store-driven tree.

The module was a demo — build the tree, write the page, flip the state on a
timer while a server streamed it. It is a test now: the two walks a reader
would have watched are asserted instead, and the page is written to a
temporary file rather than into the working directory.

The tree is the shape the demo built: two sub-roots standing in for the
"check open" and "check working" halves of a decision, below one main root.
What moves the walk is the STORE — the same graph is asked twice with a
different volatility, and the answers differ. The names carry a tag per test
because a mapping registers itself process-wide and both tests build.
"""

import os
import tempfile
import unittest

from decision_graph.decision_tree.capi import RootLogicNode, LogicMapping, NoAction, LongAction, ShortAction


def build(state, tag):
    """The demo's tree, its stores and sub-roots tagged for one test."""
    with RootLogicNode() as root:
        with LogicMapping(name=f'Root_{tag}', data=state) as lg_root:
            with lg_root.exposure == 0:
                with lg_root.working_order == 0:
                    with LogicMapping(name=f'check_open_{tag}'):
                        build_check_to_open(lg_root, tag)
                        with LogicMapping(name=f'check_open_working_{tag}'):
                            build_check_working(lg_root, tag)
                with LogicMapping(name=f'check_close_{tag}', data=state):
                    build_check_close(lg_root)
    return root


def build_check_to_open(lg_root, tag):
    with RootLogicNode(name=f'subtree_check_open_{tag}') as root:
        with lg_root.volatility > 0.25:
            with lg_root.down_prob > 0.1:
                LongAction()
            with lg_root.up_prob < -0.1:
                ShortAction()
    return root


def build_check_working(lg_root, tag):
    with RootLogicNode(name=f'subtree_check_open_working_{tag}') as root:
        with lg_root.ttl > 30:
            with lg_root.working_order > 0:
                ShortAction()
            LongAction()
    return root


def build_check_close(lg_root):
    with RootLogicNode(name='subtree_check_close') as root:
        with (lg_root.exposure > 0) & (lg_root.down_prob > 0.):
            ShortAction()
            with (lg_root.exposure < 0) & (lg_root.up_prob > 0.):
                LongAction()
    return root


def state_long_action():
    return {
        "exposure": 0,
        "working_order": 0,
        "up_prob": 0.8,
        "down_prob": 0.2,
        "volatility": 0.26,
        "ttl": 15.3
    }


def state_no_action():
    return {
        "exposure": 0,
        "working_order": 0,
        "up_prob": 0.8,
        "down_prob": 0.2,
        "volatility": 0.24,
        "ttl": 15.3
    }


class TestTheStoreDrivenWalk(unittest.TestCase):
    """Contract: the graph is built once and the store moves the answer."""

    def test_01_a_new_volatility_moves_the_next_walk(self):
        """Two walks over one graph: 0.26 crosses the volatility check, 0.24 does not."""
        state = state_long_action()
        root = build(state, 'walk')

        self.assertEqual(str(root()), '<LongAction>(sig=1)')

        state.update(state_no_action())
        self.assertEqual(str(root()), '<NoAction>(sig=0)')

    def test_02_the_walk_is_the_path_the_graph_laid(self):
        """The eval_path names every node the walk crossed, root first, leaf last."""
        root = build(state_long_action(), 'path')
        root()

        self.assertEqual(
            [str(node) for node in root.eval_path],
            [
                "<RootLogicNode>('Entry Point')",
                "<ComparisonExpression>('Root_path.exposure == 0')",
                "<ComparisonExpression>('Root_path.working_order == 0')",
                "<RootLogicNode>('subtree_check_open_path')",
                "<ComparisonExpression>('Root_path.volatility > 0.25')",
                "<ComparisonExpression>('Root_path.down_prob > 0.1')",
                '<LongAction>(sig=1)',
            ],
        )

    def test_03_the_page_is_written_and_carries_the_graph(self):
        """to_html writes a standalone page naming the sub-root the demo built."""
        root = build(state_long_action(), 'page')

        with tempfile.TemporaryDirectory() as tmp:
            page = os.path.join(tmp, 'tree.html')
            root.to_html(page)

            self.assertTrue(os.path.exists(page))
            with open(page, encoding='utf-8') as fh:
                text = fh.read()

        self.assertIn('subtree_check_open_page', text)
