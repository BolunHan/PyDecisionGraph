"""The shared vocabulary of the parity suite: one graph, three ways to run it.

WHAT THIS SUITE IS FOR
----------------------
The same decision graph can be built and run three ways, and the point of the
suite is that all three must agree:

  (a) **capi** - the established layer, built with ``with`` clauses and asked
      with ``root()``. This is the baseline: what the product already does.
  (b) **bake, walked from Python** - the bake layer, built with ``with`` clauses,
      put through ``root.bake()``, and asked the same way.
  (c) **bake, walked in C** - the same bake graph, walked by a C-side evaluator
      that never calls back into Python (``tests.bake_c_evaluator``).

Each case writes a TRANSCRIPT: one ``OBS <key> = <value>`` line per observation.
``runner.py`` runs the three in sequence, keeps each transcript in ``artifacts/``
and compares them. A transcript is the contract - the comparison is over what
the cases SAID, not over objects that only exist inside one process, which is
also what makes the artifacts readable after a failure.

THE GRAPH
---------
One store, one entry, one two-level branch::

    root
     └── signal > 0                (outer)
          ├── signal > 1           (inner)
          │    ├── LongAction
          │    └── CancelAction
          └── ClearAction

``signal`` is read three times and never copied, so feeding the store a
different value is what moves the walk - which is the behaviour every case is
really about. The values fed and the leaf each must select are ``FEED`` and
``EXPECTED`` below, written once so that two arms agreeing on a wrong answer is
still a failure.

WHAT IS COMPARED, AND WHY IT IS THIS
------------------------------------
The two layers name their nodes differently - capi wraps a generic ``LogicNode``
where bake wraps a ``BinaryExpression`` - so a comparison over wrapper class
names would fail on a correct graph. What DOES mean the same thing in both is
the decision and the shape of the walk, and that is what a transcript carries:

  - ``leaf.class``  the leaf's class name - the same string in both layers,
                    because the action family is the same family;
  - ``leaf.signal`` the leaf's ``int()``, its signal;
  - ``path.length`` how many nodes the walk crossed;
  - ``path.shape``  the walk as roles: ``R`` root, ``B`` a node with children,
                    ``L`` a leaf. Roles are structural, so both layers answer
                    the same string for the same walk.

The full path, with each node's own text, is written to the transcript too - as
a ``path.text`` line per case, for a reader of the artifact. It is NOT compared,
because the text is where the layers legitimately differ.
"""

import logging
import os
import re

# --------------------------------------------------------------------------
# The scenario
# --------------------------------------------------------------------------

# Names are short: a store's name is part of every read's display text, and the
# bake layer refuses one long enough to leave no room for an entry name.
STORE = 'parity_1'
ROOT = 'Parity.1'
ENTRY = 'signal'

# The values the entry is fed, in the order the cases walk them.
FEED = (2, 1, 0)

# What each value must select: the leaf's class name and its signal. Written
# here rather than read off any arm, so the three cases are checked against the
# DESIGN and not merely against each other.
EXPECTED = {
    2: ('LongAction', 1),
    1: ('CancelAction', 0),
    0: ('ClearAction', 0),
}

# The leaves the graph is built with, in the order each layer builds them, so
# the two builders read as the same graph written twice.
LEAVES = ('LongAction', 'CancelAction', 'ClearAction')


# --------------------------------------------------------------------------
# The scenario for case 002: the store DRIVES the walk
# --------------------------------------------------------------------------

# Three entries of three different tags, so what is exercised is not one value
# flowing down one comparison but a store whose contents decide the walk.
STORE_2 = 'parity_2'
ROOT_2 = 'Parity.2'

# ``level`` and ``ret`` are read by the comparisons; ``armed`` gates the whole
# branch, which is what makes an entry of a different tag part of the decision
# rather than decoration.
LEVEL = 'level'
RET = 'ret'
ARMED = 'armed'

# The graph::
#
#     root
#      └── (level > 2) & armed          gate: a comparison AND an entry
#           ├── ret > 0.5               inner: a float comparison
#           │    ├── LongAction
#           │    └── CancelAction
#           └── ShortAction
#
# Three levels and three entries, which is the point: the gate's value is
# computed from TWO entries of different tags, so a walk cannot be right unless
# both were read - and the leaf below the gate is chosen by a third.
#
# Every value here is fed into an entry a read names, so every row below is a
# statement about the STORE: one graph, one bake, different contents, different
# leaf.
FEED_2 = (
    (3, 0.9, True),    # level>2 and armed, ret>0.5  -> the inner TRUE arm
    (3, 0.1, True),    # level>2 and armed, ret<=0.5 -> the inner FALSE arm
    (3, 0.9, False),   # armed is false               -> the gate's FALSE arm
    (2, 0.9, True),    # level is not >2              -> the gate's FALSE arm
    (0, 0.0, False),   # both halves false            -> the gate's FALSE arm
)

EXPECTED_2 = {
    (3, 0.9, True): ('LongAction', 1),
    (3, 0.1, True): ('CancelAction', 0),
    (3, 0.9, False): ('ShortAction', -1),
    (2, 0.9, True): ('ShortAction', -1),
    (0, 0.0, False): ('ShortAction', -1),
}


def feed_order_2(feed=FEED_2):
    """The entry triples to feed, in order, with the round each belongs to."""
    return list(enumerate(feed))


def check_expected_2(triple, leaf_class: str, signal: int) -> None:
    """Assert one walk produced the leaf the design calls for."""
    want_class, want_signal = EXPECTED_2[triple]
    if (leaf_class, signal) != (want_class, want_signal):
        raise AssertionError(
            f'feeding {LEVEL}, {RET}, {ARMED} = {triple} selected '
            f'{leaf_class}(sig={signal}); the design calls for '
            f'{want_class}(sig={want_signal})'
        )


# --------------------------------------------------------------------------
# The transcript
# --------------------------------------------------------------------------

ARTIFACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'artifacts')

# What a case's own pytest run leaves behind, beside its transcript.
OBSERVATION_PREFIX = 'OBS'
_OBSERVATION = re.compile(r'^OBS (.+?) = (.*)$')


class Recorder:
    """One case's transcript, written to its own file under ``artifacts/``.

    The file is truncated on open: a run's transcript is that run's, so a stale
    line from a previous walk can never be read as this one's.

    The logger is named per case and does not propagate, so a case's transcript
    is exactly what that case emitted and nothing the suite around it logged.
    """

    def __init__(self, case: str, group: str):
        self.case = case
        self.path = os.path.join(ARTIFACTS, group, f'{case}.log')
        os.makedirs(os.path.dirname(self.path), exist_ok=True)

        self._logger = logging.getLogger(f'bake_parity.{group}.{case}')
        self._logger.handlers = [logging.FileHandler(self.path, mode='w')]
        self._logger.setLevel(logging.INFO)
        self._logger.propagate = False
        self._closed = False

    def emit(self, key: str, value) -> None:
        """Write one observation to the transcript."""
        self._logger.info('%s %s = %s', OBSERVATION_PREFIX, key, value)

    def close(self) -> None:
        """Flush and detach, so the file is complete on disk when we return."""
        if self._closed:
            return
        for handler in self._logger.handlers:
            handler.flush()
            handler.close()
        self._logger.handlers = []
        self._closed = True


def read_transcript(path: str) -> dict:
    """The observations a transcript holds, as an ordered mapping."""
    observations = {}
    with open(path, encoding='utf-8') as fh:
        for line in fh:
            match = _OBSERVATION.match(line.rstrip('\n'))
            if match:
                observations[match.group(1)] = match.group(2)
    return observations


# Two kinds of line a transcript carries that no comparison should touch, and
# the difference between them is why there are two:
#
#   - ``arm.``   a fact about THIS arm - which door it walked through, what its
#                own bake report said. True of one arm and not of the others, so
#                comparing it would fail on a correct graph.
#   - ``local.`` a line written FOR A READER, in the arm's own words. The walk's
#                full text is the case in point: the two layers name a branch
#                differently by design - capi's is a ``LogicNode`` and bake's a
#                ``BinaryExpression`` - and the artifact is where that is worth
#                seeing. The roles (``path.shape``) are what is compared.
#
# Everything else is the contract, and must agree across the arms.
LOCAL_PREFIXES = ('arm.', 'local.')


def comparable(observations: dict) -> dict:
    """The part of a transcript the three arms must agree on."""
    return {key: value for key, value in observations.items()
            if not key.startswith(LOCAL_PREFIXES)}


def arm_local(observations: dict) -> dict:
    """The part of a transcript written for a reader rather than for a compare."""
    return {key: value for key, value in observations.items()
            if key.startswith(LOCAL_PREFIXES)}


def transcript_path(case: str, group: str) -> str:
    """Where a case's transcript is written."""
    return os.path.join(ARTIFACTS, group, f'{case}.log')


def arm_names(case: str, store: str, root: str) -> tuple:
    """The store and root names one ARM builds under, with the arm appended.

    An arm is meant to run in a process of its own - that is how the runner
    keeps its transcript separable - but nothing stops ``pytest`` from
    collecting all three arms of a case into ONE process, and then two arms
    building a store called ``parity_1`` is a name already taken: groups
    register themselves by name, and the second one is refused rather than
    shadowing the first.

    Suffixing the name with the arm makes the arms independent of how they were
    launched. The names are then a fact about the arm and not about the graph,
    so a transcript carries them under ``arm.`` - what the graph names is the
    ENTRIES, and those are the same in every arm.
    """
    return f'{store}.{case}', f'{root}.{case}'


# --------------------------------------------------------------------------
# What a case observes
# --------------------------------------------------------------------------

def shape_of(nodes, root) -> str:
    """The walk as roles: ``R`` root, ``B`` a node with children, ``L`` a leaf.

    Roles rather than class names, because a role is what the two layers can
    both answer for the same walk: capi's branch is a ``LogicNode`` and bake's
    is a ``BinaryExpression``, and both are a node the walk descended through.
    """
    roles = []
    for node in nodes:
        if node is root:
            roles.append('R')
        elif node.is_leaf:
            roles.append('L')
        else:
            roles.append('B')
    return '>'.join(roles)


def feed_order(feed=FEED):
    """The values to feed, in order, with the round each belongs to."""
    return list(enumerate(feed))


def check_expected(value, leaf_class: str, signal: int) -> None:
    """Assert one walk produced the leaf and signal the design calls for."""
    want_class, want_signal = EXPECTED[value]
    if (leaf_class, signal) != (want_class, want_signal):
        raise AssertionError(
            f'feeding {ENTRY}={value} selected {leaf_class}(sig={signal}); '
            f'the design calls for {want_class}(sig={want_signal})'
        )
