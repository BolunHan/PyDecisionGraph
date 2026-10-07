"""The web UIs, one per layer.

Each UI is a copy of the same code that reads a DIFFERENT API, so what these
cases pin is that separation: the capi UI names the capi classes, the native UI
names the native ones, and neither reaches the package that re-exports whichever
layer won at import time.
"""

import pathlib
import tempfile
import unittest

from decision_graph.decision_tree import capi as capi_api
from decision_graph.decision_tree import native as native_api
from decision_graph.webui import capi as capi_ui
from decision_graph.webui import native as native_ui


def build_tree(mod) -> object:
    """A two-branch tree, built through one layer's own surface."""

    def node(name: str, value: bool):
        return mod.LogicNode(expression=value, dtype=bool, repr=f'{name}, {value}')

    original_mode = mod.LGM.inspection_mode
    mod.LGM.inspection_mode = True
    try:
        with mod.LogicGroup(name='ui_layer'):
            with node('root', True) as root:
                with node('yes', True):
                    mod.LongAction()
                with node('no', False):
                    mod.NoAction()
                    mod.ShortAction()
    finally:
        mod.LGM.inspection_mode = original_mode
    return root


class TestEachUiReadsItsOwnLayer(unittest.TestCase):
    """The scoped import is the whole point of the split."""

    def test_01_the_capi_ui_names_the_capi_classes(self):
        from decision_graph.webui.capi.app import LogicNode as Scoped
        self.assertIs(Scoped, capi_api.LogicNode)
        self.assertTrue(Scoped.__module__.startswith('decision_graph.decision_tree.capi'))

    def test_02_the_native_ui_names_the_native_classes(self):
        from decision_graph.webui.native.app import LogicNode as Scoped
        self.assertIs(Scoped, native_api.LogicNode)
        self.assertTrue(Scoped.__module__.startswith('decision_graph.decision_tree.native'))

    def test_03_the_two_layers_are_not_the_same_objects(self):
        self.assertIsNot(capi_api.LogicNode, native_api.LogicNode)
        self.assertIsNot(capi_ui.DecisionTreeWebUi, native_ui.DecisionTreeWebUi)

    def test_04_the_layer_modules_are_two_different_packages(self):
        self.assertNotEqual(
            capi_ui.DecisionTreeWebUi.__module__.split('.')[:4],
            native_ui.DecisionTreeWebUi.__module__.split('.')[:4],
        )


class TestTheOfflineExportForEachLayer(unittest.TestCase):
    """to_html, run per layer, over the same shape of tree."""

    def _export(self, ui, api) -> str:
        root = build_tree(api)
        with tempfile.TemporaryDirectory() as tmp:
            target = pathlib.Path(tmp) / 'tree.html'
            ui.to_html(root, str(target))
            self.assertTrue(target.is_file(), 'the export wrote no file')
            html = target.read_text(encoding='utf-8')
            self.assertIn('root, True', html, 'the export does not name the root')
            self.assertIn('ShortAction', html, 'the export does not name a leaf')
            return html

    def test_01_capi_export(self):
        html = self._export(capi_ui, capi_api)
        self.assertIn('<html', html)

    def test_02_native_export(self):
        html = self._export(native_ui, native_api)
        self.assertIn('<html', html)


if __name__ == '__main__':
    unittest.main()
