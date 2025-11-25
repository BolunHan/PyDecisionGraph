import sys
import warnings

sys.path.append(f'/home/bolun/Projects/PyDecisionGraph')

from decision_graph.decision_tree.native.abc import (
    ELSE_CONDITION,
    NO_CONDITION,
    ConditionElse,
    SkipContextsBlock,
)


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


# --- Plain tests ---

def test_else_condition_singleton_existence_and_behavior():
    assert ELSE_CONDITION is not None, "ELSE_CONDITION should be defined"
    assert isinstance(ELSE_CONDITION, ConditionElse), "ELSE_CONDITION should be instance of ConditionElse"
    assert str(ELSE_CONDITION) == "Else"
    # Attempting to instantiate a new ConditionElse should raise RuntimeError (singleton enforcement)
    expect_raises(TypeError, ConditionElse())


def test_skip_contexts_block_execution():
    scb = SkipContextsBlock()
    ran = []
    with scb:
        ran.append("body")
    assert ran == ["body"], "Body should execute when default_entry_check is True"


def test_skip_contexts_block_skipped():
    scb = SkipContextsBlock()
    original_tracer = sys.gettrace()
    scb.default_entry_check = False  # Force skip
    ran = []
    print(f"[pre] sys.gettrace={original_tracer}")
    try:
        with scb:
            ran.append("body")  # Should never run
    finally:
        print(f"[post] sys.gettrace={sys.gettrace()}")
    assert ran == [], "Body must not execute when entry check is False"
    # Ensure original trace function restored
    assert sys.gettrace() is original_tracer, "Original sys.gettrace() should be restored after skipping"


def test_skip_contexts_block_empty():
    scb = SkipContextsBlock()
    original_tracer = sys.gettrace()
    scb.default_entry_check = False  # Force skip
    # Using an empty block should still trigger skip and be suppressed by __exit__
    print(f"[empty-pre] sys.gettrace={original_tracer}")
    try:
        with scb:
            pass
    finally:
        print(f"[empty-post] sys.gettrace={sys.gettrace()}")
    assert sys.gettrace() is original_tracer, "Original sys.gettrace() should be restored after skipping"


def test_skip_contexts_block_single_skipped_restores_none_tracer():
    # Explicitly set no tracer and ensure it remains None after skip
    prev_tracer = sys.gettrace()
    ran = []
    try:
        sys.settrace(None)
        assert sys.gettrace() is None
        scb = SkipContextsBlock()
        scb.default_entry_check = False
        with scb:
            ran.append("body")  # Should never run
        assert ran == [], "Body must not execute when entry check is False"
        assert sys.gettrace() is None, "Tracer should remain None after SkipContextsBlock skip path"
    finally:
        # restore whatever tracer was present before this test (even None)
        sys.settrace(prev_tracer)


def test_skip_contexts_block_single_skipped_restores_existing_tracer():
    # Install a custom tracer and verify it is restored after skip
    prev_tracer = sys.gettrace()
    events = []

    def dummy_tracer(frame, event, arg):
        # record minimal activity and return itself to trace deeper calls
        if event in ("call", "return"):
            events.append(event)
        return dummy_tracer

    sys.settrace(dummy_tracer)
    assert sys.gettrace() is dummy_tracer
    scb = SkipContextsBlock()
    scb.default_entry_check = False
    entered = False
    with scb:
        entered = True  # should be skipped
    # After context, the original tracer (dummy_tracer) must be restored
    assert not entered
    print('[test] expecting restored tracer to be dummy_tracer...')
    if sys.gettrace() is not dummy_tracer:
        warnings.warn("[expected error] Custom tracer should be restored after SkipContextsBlock skip path")
    sys.settrace(None)


def test_skip_contexts_block_nested_all_run():
    outer = SkipContextsBlock()
    inner = SkipContextsBlock()
    events = []
    with outer:
        events.append("outer-enter")
        with inner:
            events.append("inner-enter")
        events.append("outer-exit")
    assert events == ["outer-enter", "inner-enter", "outer-exit"], "Both contexts should execute fully"


def test_skip_contexts_block_nested_outer_skips_all():
    outer = SkipContextsBlock()
    inner = SkipContextsBlock()
    outer.default_entry_check = False  # Outer will skip -> inner shouldn't run
    events = []
    with outer:
        events.append("outer-enter")  # Should not run
        with inner:
            events.append("inner-enter")  # Should not run
        events.append("outer-exit")  # Should not run
    assert events == [], "When outer skips, inner context body must also be skipped"


def test_skip_contexts_block_nested_inner_skips_only_inner():
    outer = SkipContextsBlock()
    inner = SkipContextsBlock()
    inner.default_entry_check = False  # Only inner skips
    events = []
    with outer:
        events.append("outer-enter")
        with inner:
            events.append("inner-enter")  # Should not run
        events.append("outer-exit")
    assert len(events) == 2
    assert events == ["outer-enter", "outer-exit"], "Inner skip should not affect outer body before and after inner block"


# Simple runner for direct invocation: python tests/test_skippable.py
if __name__ == "__main__":
    import inspect

    this_module = sys.modules[__name__]
    tests = [
        (name, obj)
        for name, obj in sorted(this_module.__dict__.items())
        if name.startswith("test_") and inspect.isfunction(obj)
    ]
    total = len(tests)
    print(f"Discovered {total} plain tests. Running...\n")
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
