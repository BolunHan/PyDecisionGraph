from decision_graph.decision_tree.c_abc import LGM, LogicNode, LongAction, ShortAction, NoAction, LogicGroup
from random import choice


def node(name: str, v: bool = None):
    if v is None:
        v = choice([True, False])
    ln = LogicNode(
        expression=v,
        dtype=bool,
        repr=f'{name}, {v}'
    )
    return ln


def group(name: str):
    lg = LogicGroup(name=name)
    return lg


LGM.inspection_mode = True

with node(name='root') as root:
    with node(name='child_1'):
        with node(name='child_1_1'):
            LongAction()

        with node(name='child_1_2'):
            NoAction()
            ShortAction()

    with node(name='child_2'):
        pass

print(root)

with group('root_group') as root_group:
    with node('root', True) as root:
        with group('group_1') as group_1:
            with node('child_1', True):
                with node('child_1_1', True):
                    group_1.break_()
                    LongAction()

        with node('child_2', False):
            NoAction()
            ShortAction()

print(root)
v, p = root.eval_recursively()
print(f'Eval result: {v}, Path: {p}')
