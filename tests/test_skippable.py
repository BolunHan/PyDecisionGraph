import sys

sys.path.append(f'/home/bolun/Projects/PyDecisionGraph')
import unittest

from decision_graph.decision_tree.c_abc import (
    ELSE_CONDITION,
    NO_CONDITION,
    ConditionElse,
    SkipContextsBlock,
)


class TestConditionElse(unittest.TestCase):
    def test_else_condition_singleton_existence_and_behavior(self):
        # 1. global ELSE_CONDITION exists and is the same as NO_CONDITION
        self.assertIsNotNone(ELSE_CONDITION, "ELSE_CONDITION should be defined")
        self.assertIs(ELSE_CONDITION, NO_CONDITION, "ELSE_CONDITION and NO_CONDITION must reference the same singleton")
        self.assertIsInstance(ELSE_CONDITION, ConditionElse, "ELSE_CONDITION should be instance of ConditionElse")
        # String/representation checks
        self.assertEqual(str(ELSE_CONDITION), "")
        # Attempting to instantiate a new ConditionElse should raise RuntimeError (singleton enforcement)
        with self.assertRaises(RuntimeError):
            ConditionElse()


class TestSingleSkipContextsBlock(unittest.TestCase):
    def test_skip_contexts_block_execution(self):
        scb = SkipContextsBlock()
        ran = []
        with scb:
            ran.append("body")
        self.assertEqual(ran, ["body"], "Body should execute when default_entry_check is True")

    def test_skip_contexts_block_skipped(self):
        scb = SkipContextsBlock()
        original_tracer = sys.gettrace()
        scb.default_entry_check = False  # Force skip
        ran = []
        with scb:
            ran.append("body")  # Should never run
        self.assertEqual(ran, [], "Body must not execute when entry check is False")
        # Ensure original trace function restored
        self.assertIs(sys.gettrace(), original_tracer, "Original sys.gettrace() should be restored after skipping")

    def test_skip_contexts_block_empty(self):
        scb = SkipContextsBlock()
        original_tracer = sys.gettrace()
        scb.default_entry_check = False  # Force skip
        with scb:
            pass  # Should raise skip_exception
        # Ensure original trace function restored
        self.assertIs(sys.gettrace(), original_tracer, "Original sys.gettrace() should be restored after skipping")

    def test_skip_contexts_block_single_skipped_restores_none_tracer(self):
        # Explicitly set no tracer and ensure it remains None after skip
        prev_tracer = sys.gettrace()
        ran = []
        try:
            sys.settrace(None)
            self.assertIsNone(sys.gettrace())
            scb = SkipContextsBlock()
            scb.default_entry_check = False
            with scb:
                ran.append("body")  # Should never run
            self.assertEqual(ran, [], "Body must not execute when entry check is False")
            self.assertIsNone(sys.gettrace(), "Tracer should remain None after SkipContextsBlock skip path")
        finally:
            # restore whatever tracer was present before this test (even None)
            sys.settrace(prev_tracer)

    def test_skip_contexts_block_single_skipped_restores_existing_tracer(self):
        # Install a custom tracer and verify it is restored after skip
        prev_tracer = sys.gettrace()
        events = []

        def dummy_tracer(frame, event, arg):
            # record minimal activity and return itself to trace deeper calls
            if event in ("call", "return"):
                events.append(event)
            return dummy_tracer

        try:
            sys.settrace(dummy_tracer)
            self.assertIs(sys.gettrace(), dummy_tracer)
            scb = SkipContextsBlock()
            scb.default_entry_check = False
            entered = False
            with scb:
                # events.append('entered')
                entered = True  # should be skipped
            # After context, the original tracer (dummy_tracer) must be restored
            assert not entered
            self.assertIs(sys.gettrace(), dummy_tracer, "Custom tracer should be restored after SkipContextsBlock skip path")
        finally:
            sys.settrace(prev_tracer)

    def test_skip_contexts_block_nested_all_run(self):
        outer = SkipContextsBlock()
        inner = SkipContextsBlock()
        events = []
        with outer:
            events.append("outer-enter")
            with inner:
                events.append("inner-enter")
            events.append("outer-exit")
        self.assertEqual(events, ["outer-enter", "inner-enter", "outer-exit"], "Both contexts should execute fully")

    def test_skip_contexts_block_nested_outer_skips_all(self):
        outer = SkipContextsBlock()
        inner = SkipContextsBlock()
        outer.default_entry_check = False  # Outer will skip -> inner shouldn't run
        events = []
        with outer:
            events.append("outer-enter")  # Should not run
            with inner:
                events.append("inner-enter")  # Should not run
            events.append("outer-exit")  # Should not run
        self.assertEqual(events, [], "When outer skips, inner context body must also be skipped")

    def test_skip_contexts_block_nested_inner_skips_only_inner(self):
        outer = SkipContextsBlock()
        inner = SkipContextsBlock()
        inner.default_entry_check = False  # Only inner skips
        events = []
        with outer:
            events.append("outer-enter")
            with inner:
                events.append("inner-enter")  # Should not run
            events.append("outer-exit")
        self.assertEqual(len(events), 2)
        self.assertEqual(events, ["outer-enter", "outer-exit"], "Inner skip should not affect outer body before and after inner block")


if __name__ == "__main__":
    unittest.main()
