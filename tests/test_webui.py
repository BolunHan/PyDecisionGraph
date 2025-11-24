from decision_graph.decision_tree import RootLogicNode, LogicMapping, NoAction, LongAction, ShortAction, LGM


def build(state):
    with RootLogicNode() as root:
        with LogicMapping(name='Root', data=state) as lg_root:
            with lg_root.exposure == 0:
                with lg_root.working_order == 0:
                    with LogicMapping(name='check_open'):
                        build_check_to_open(lg_root)
                        with LogicMapping(name='check_open_working'):
                            build_check_working(lg_root)
                # having exposure
                with LogicMapping(name='check_close', data=state) as lg:
                    build_check_close(lg_root)
    return root


def build_check_to_open(lg_root):
    with RootLogicNode(name='subtree_check_open') as root:
        with lg_root.volatility > 0.25:
            with lg_root.down_prob > 0.1:
                LongAction()
            with lg_root.up_prob < -0.1:
                ShortAction()
    return root


def build_check_working(lg_root):
    with RootLogicNode(name='subtree_check_open_working') as root:
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


def node(name: str, v: bool = None):
    from decision_graph.decision_tree.capi import LogicNode
    if v is None:
        from random import choice
        v = choice([True, False])
    ln = LogicNode(
        expression=v,
        dtype=bool,
        repr=f'{name}, {v}'
    )
    return ln


def build_dummy():
    LGM.inspection_mode = True
    with node('a') as root:
        with node('b', True):
            LongAction()
        with node('c', False):
            NoAction()
            ShortAction()
    return root


def main():
    state = {
        "exposure": 0,
        "working_order": 0,
        "up_prob": 0.8,
        "down_prob": 0.2,
        "volatility": 0.24,
        "ttl": 15.3
    }

    root = build(state)
    # root = build_dummy()
    from decision_graph.decision_tree.webui import show, to_html
    root.to_html()
    show(root, with_eval=True)


if __name__ == '__main__':
    main()
