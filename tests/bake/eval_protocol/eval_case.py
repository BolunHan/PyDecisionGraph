"""The base every eval-protocol case shares: a store of its own, and the doors.

The suite is split by SUBJECT - one file per group of cases - and this module is
the little they have in common. What it supplies is the two things every case
needs and neither the layer nor unittest gives it:

  - a store of its own, because entry names and store names are process-wide and
    a store name ends up inside every read's display text;
  - the doors, written once: a read is built inside the store that owns it, and a
    literal is three words shorter.

Two traps worth writing down, because this suite is full of nodes:

  - ``==`` on two nodes COMPOSES a comparison node, it does not compare wrappers.
    Every identity assertion here is ``assertIs`` / ``assertIsNot``, and every
    value assertion is on a Python value the node produced.
  - an unevaluated node holds nothing, so ``out.is_null`` is how "this node was
    not reached" is observed - ``out.value`` cannot tell that from a node that
    produced None.
"""

import itertools
import unittest

from decision_graph.decision_tree.bake.c_collections import LogicMapping
from decision_graph.decision_tree.bake.c_const import ConstantNode

# Store names are short: they end up in every read's display text.
_store_tag = 'ep'
_store_counter = itertools.count()


def store_name() -> str:
    """A store name no other case in this process has taken."""
    return f'{_store_tag}{next(_store_counter)}'


class EvalCase(unittest.TestCase):
    """A case with a store of its own, and the two doors the cases share."""

    def setUp(self) -> None:
        self.mapping = LogicMapping(name=store_name())

    def read(self, key: str):
        """The read of an entry in this case's store, built and owned by it.

        A read is built inside the store it reads - that is what makes it the
        store's - and what comes back outlives the block: the store holds it.
        """
        with self.mapping:
            return self.mapping[key]

    @staticmethod
    def const(value):
        """A literal node for a Python value."""
        return ConstantNode(value)

    def fill(self, **entries) -> None:
        """Put values in entries, reserving the ones that are new."""
        for key, value in entries.items():
            self.mapping[key] = value

    def show(self, label: str, *values) -> None:
        """Print one line of evidence, so a run can be read afterwards."""
        print(f'{label:<28} ' + '  '.join(str(value) for value in values))
