import sys

from decision_graph.decision_tree.c_abc import SkipContextsBlock, EmptyBlock

def main():
    scb = SkipContextsBlock()
    scb.default_entry_check = False
    print('trying skipping printing code...')
    with scb:
        print('never enter!')

    l = []
    print('trying skipping appending (c calls)')
    with scb:
        l.append('never enter!')
    print(l)

    print('trying skipping assignment')
    entered = False
    with scb:
        entered = True
    assert not entered

    print('trying skipping pass (empty block)')
    with scb:
        pass

    print('trying skipping hybrid block')
    entered = False
    with scb:
        pass
        entered = True
        print('never enter!')
    assert not entered

    print('trying skipping with custom tracer')
    events = []
    entered = False
    def dummy_tracer(frame, event, arg):
        # record minimal activity and return itself to trace deeper calls
        if event in ("call", "return"):
            events.append(event)
        return dummy_tracer

    # sys.settrace(dummy_tracer)
    with scb:
        (lambda x: x + 1)(10)
        entered = True  # should be skipped
    # After context, the original tracer (dummy_tracer) must be restored
    assert not entered


if __name__ == '__main__':
    main()
