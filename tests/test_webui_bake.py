"""The bake layer's viewer: what it reads, and what it writes out.

The page is one half of a contract and the payload is the other, so what these
cases pin is the payload: the card model the serializer builds, what it counts
as scaffolding, and the walk it carries. The drawing itself is checked by eye -
``tests/bake_webui_artifacts.py`` writes pages deep enough to read.
"""

import itertools
import pathlib
import tempfile
import unittest

from decision_graph.decision_tree import bake as bake_api
from decision_graph.decision_tree.bake.c_action import ClearAction, LongAction, ShortAction
from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_expr import BinaryExpression, ExpressionOperator
from decision_graph.decision_tree.bake.c_const import ConstantNode
from decision_graph.decision_tree.bake.c_hierarchy import BreakpointNode, RootLogicNode
from decision_graph.decision_tree.bake.c_logic_group import LogicGroup
from decision_graph.decision_tree.bake.c_node import LogicNode
from decision_graph.webui import bake as bake_ui
from decision_graph.webui.bake.app import BakeWebUi

_counter = itertools.count()


def _name(stem: str) -> str:
    """A unique name: both store and group names are registered process-wide."""
    return f'{stem}{next(_counter)}'


def build_tree(with_break: bool = False):
    """A three-branch tree over one store, optionally with a break in it.

    The break is raised and carried on from inside the same build, which is what
    leaves a breakpoint standing in the graph: it is the bake that takes it down,
    not the build.
    """
    store = LogicMapping(name=_name('view'))
    with RootLogicNode(name='View Test') as root:
        with store:
            signal = store['signal']
            with signal > 0:
                with signal > 1:
                    LongAction()
                    ClearAction()
                # The statement after a `with` block fills the OTHER arm of the
                # node that block opened - not another arm of the node above it.
                if with_break:
                    with LogicGroup(name=_name('chk')) as checks:
                        with signal > 2:
                            ShortAction()
                            breakpoint = BreakpointNode.break_(break_from=checks)
                    with breakpoint:
                        with signal > 3:
                            LongAction()
                            ShortAction()
                else:
                    with signal > 2:
                        ShortAction()
                        LongAction()
    return root, store


def collect(payload: dict) -> list[dict]:
    """Every card in the payload, depth first."""
    found = []

    def walk(record: dict) -> None:
        if record.get('is_reference'):
            return
        found.append(record)
        for arm in record.get('_children', []):
            walk(arm['node'])

    walk(payload['root'])
    return found


class TestTheViewerReadsTheBakeLayer(unittest.TestCase):
    """The scoped import: this UI is the bake layer's, and says so."""

    def test_01_the_viewer_names_the_bake_classes(self):
        from decision_graph.webui.bake.app import LogicNode as Scoped
        self.assertIs(Scoped, bake_api.LogicNode)
        self.assertIs(Scoped, LogicNode)
        self.assertTrue(Scoped.__module__.startswith('decision_graph.decision_tree.bake'))

    def test_02_the_viewer_lives_under_webui_not_under_decision_tree(self):
        self.assertEqual(BakeWebUi.__module__, 'decision_graph.webui.bake.app')
        self.assertEqual(bake_ui.BakeWebUi, BakeWebUi)

    def test_03_the_three_viewers_are_three_different_classes(self):
        from decision_graph.webui import capi as capi_ui
        from decision_graph.webui import native as native_ui
        self.assertIsNot(BakeWebUi, capi_ui.DecisionTreeWebUi)
        self.assertIsNot(BakeWebUi, native_ui.DecisionTreeWebUi)


class TestTheCardModel(unittest.TestCase):
    """What the page is handed, before any drawing."""

    def test_01_a_branch_becomes_a_card_with_one_arm_per_condition(self):
        root, store = build_tree()
        payload = BakeWebUi._convert_tree_to_format(root)

        self.assertEqual(payload['root']['type'], 'ROOT')
        cards = collect(payload)
        types = [card['type'] for card in cards]
        # The root, three two-way nodes, and the four leaves below them.
        self.assertEqual(types.count('BINARY'), 3)
        self.assertEqual(len(cards), 8)
        self.assertTrue(all(t in ('ROOT', 'BINARY', 'LONGACTION', 'SHORTACTION', 'CLEARACTION')
                            for t in types), types)

        # A two-way node carries exactly two arms, one per binary truth value.
        top = payload['root']['_children'][0]['node']
        conditions = [arm['condition']['value'] for arm in top['_children']]
        self.assertEqual(conditions, [True, False])

    def test_02_a_card_carries_the_metadata_the_page_draws(self):
        root, store = build_tree()
        payload = BakeWebUi._convert_tree_to_format(root)
        card = payload['root']['_children'][0]['node']

        self.assertEqual(card['family'], 'OP')
        self.assertIn(store.name, card['labels'])
        self.assertFalse(card['is_leaf'])
        self.assertIsInstance(card['address'], int)
        self.assertGreater(card['size'], 1)
        self.assertEqual(card['hooks'], [])
        self.assertIsNone(card['out'], 'a node nobody has walked holds nothing')

    def test_03_the_root_names_itself_and_counts_what_is_drawn(self):
        root, store = build_tree()
        payload = BakeWebUi._convert_tree_to_format(root)
        self.assertEqual(payload['root_name'], 'View Test')
        self.assertEqual(payload['n_nodes'], len(collect(payload)))


class TestWhatItCountsAsScaffolding(unittest.TestCase):
    """The one thing that tells a build's graph from a baked one."""

    def test_01_a_clean_build_carries_none(self):
        root, store = build_tree(with_break=False)
        payload = BakeWebUi._convert_tree_to_format(root)
        self.assertEqual(payload['scaffolding'], {'breakpoints': 0, 'placeholders': 0})

    def test_02_a_break_left_standing_is_counted(self):
        root, store = build_tree(with_break=True)
        payload = BakeWebUi._convert_tree_to_format(root)
        self.assertEqual(payload['scaffolding']['breakpoints'], 1)
        self.assertIn('BREAKPOINT', [card['type'] for card in collect(payload)])

    def test_03_the_bake_takes_it_down_and_the_count_goes_with_it(self):
        root, store = build_tree(with_break=True)
        before = collect(BakeWebUi._convert_tree_to_format(root))
        root.bake()
        after = collect(BakeWebUi._convert_tree_to_format(root))

        self.assertEqual([c['type'] for c in before].count('BREAKPOINT'), 1)
        self.assertEqual([c['type'] for c in after].count('BREAKPOINT'), 0)
        self.assertEqual(BakeWebUi._convert_tree_to_format(root)['scaffolding']['breakpoints'], 0)

    def test_04_the_continuation_is_drawn_once_and_under_the_break(self):
        root, store = build_tree(with_break=True)
        payload = BakeWebUi._convert_tree_to_format(root)
        break_card = next(card for card in collect(payload) if card['type'] == 'BREAKPOINT')

        self.assertEqual(len(break_card['_children']), 1)
        arm = break_card['_children'][0]
        self.assertTrue(arm['condition']['is_else'], 'a break resumes on the else arm')
        # A real card carries its record; a reference is only an id and a flag.
        self.assertIn('type', arm['node'])
        self.assertEqual(arm['node']['type'], 'BINARY')


class TestTheWalkItCarries(unittest.TestCase):
    """The evaluation record, read and never taken: the page walks on request."""

    def test_01_a_graph_nobody_walked_carries_no_walk(self):
        root, store = build_tree()
        payload = BakeWebUi._convert_tree_to_format(root)
        self.assertIsNone(payload['walk'])
        self.assertEqual(payload['active_ids'], [])
        self.assertTrue(all(card['activated'] for card in collect(payload)),
                        'with no walk to show, nothing is dimmed')

    def test_02_a_walked_graph_carries_its_path(self):
        root, store = build_tree()
        root.bake()
        store['signal'] = 5
        decision = root()

        payload = BakeWebUi._convert_tree_to_format(root, BakeWebUi._walk_record(root))
        self.assertEqual(payload['walk']['code'], 'OK')
        self.assertTrue(payload['walk']['ids'])
        self.assertEqual(payload['walk']['length'], len(payload['walk']['ids']))
        self.assertIsNotNone(payload['walk']['leaf'])
        self.assertEqual(decision.type, 'LONGACTION')

        on_path = [card for card in collect(payload) if card['activated']]
        off_path = [card for card in collect(payload) if not card['activated']]
        self.assertTrue(on_path, 'the path names cards the page can highlight')
        self.assertTrue(off_path, 'and leaves the ones it did not reach unlit')

    def test_03_the_walk_is_passed_over_when_the_caller_does_not_want_it(self):
        root, store = build_tree()
        root.bake()
        store['signal'] = 5
        root()
        payload = BakeWebUi._convert_tree_to_format(root, None)
        self.assertIsNone(payload['walk'])


class TestTheOfflineExport(unittest.TestCase):
    """to_html: the page the server would have served, in one file."""

    def _export(self, root, **kwargs) -> str:
        with tempfile.TemporaryDirectory() as tmp:
            target = pathlib.Path(tmp) / 'graph.html'
            bake_ui.to_html(root, str(target), **kwargs)
            self.assertTrue(target.is_file(), 'the export wrote no file')
            return target.read_text(encoding='utf-8')

    def test_01_the_export_is_self_contained(self):
        root, store = build_tree()
        html = self._export(root)
        self.assertIn('Bake Graph', html)
        self.assertIn('bake-viewport', html)
        self.assertIn('id="tree-data"', html)
        # No server, no network: the stylesheet, the script and D3 are inlined.
        self.assertNotIn('url_for', html)
        self.assertIn('d3.zoom', html)
        self.assertIn('--card-bg', html)

    def test_02_the_export_carries_the_graph(self):
        root, store = build_tree(with_break=True)
        html = self._export(root)
        self.assertIn('View Test', html)
        self.assertIn('BREAKPOINT', html)
        self.assertIn(store.name, html)

    def test_03_the_scaffolding_is_what_makes_a_bake_visible(self):
        root, store = build_tree(with_break=True)
        before = self._export(root)
        root.bake()
        after = self._export(root)
        self.assertNotEqual(before, after, 'the bake changed the graph, so it changes the page')
        self.assertIn('BREAKPOINT', before)
        self.assertNotIn('BREAKPOINT', after)

    def test_04_the_export_refuses_a_non_node(self):
        with self.assertRaises(TypeError):
            bake_ui.to_html(object(), 'nowhere.html')


if __name__ == '__main__':
    unittest.main()
