# === ENVIRONMENT SETUP ===

STATE = {
    'predicted_return': 0.05,
    'risk_level': 1,  # 0: safe, 1: medium, 2: risky
}

# ==== LGM Managed Example ====
from decision_graph.decision_tree import RootLogicNode, LogicMapping, LongAction, ShortAction, LogicGroup, NoAction, BreakpointNode, LGM

with RootLogicNode() as root:
    with LogicMapping(name='Market Analysis', data=STATE) as lg:
        with lg.predicted_return > 0.03:
            with lg.risk_level > 1:
                with LogicGroup(name='OpenCheck.Long') as lg_long:
                    LogicGroup.break_(lg_long)
                    # A deeper decision tree can be built here, for simplicity we just add a single action
                    LongAction()
            with LogicGroup(name='OpenCheck.Short') as lg_short:
                with lg.predicted_return < -0.03:
                    with lg.risk_level < 1:
                        ShortAction()
                        # All the remaining NoAction path can be omitted
                        # NoAction()  # Optional, for clarity
                    # NoAction()  # Optional, for clarity

# With this ``LogicGroup.break_(...)`` style breakpoint, the virtual connections are handled by LGM automatically.
# You can visualize the decision tree to see how the breakpoints affect the flow.
# Expected to see a virtual linkage from the breakpoint, to the `lg.predicted_return < -0.03` node.
root.show(with_eval=False)

# ==== Manual Breakpoint Example ====
LGM.clear()
with RootLogicNode() as root:
    with LogicMapping(name='Market Analysis', data=STATE) as lg:
        with LogicGroup(name='OuterOpenCheck.Long') as lg_long:
            with lg.predicted_return > 0.03:
                with lg.risk_level > 1:
                    # OuterOpenCheck.Long logic group now covers the entrance checking node
                    # This time, if we wish to let the lg_short outside the OuterOpenCheck.Long
                    # A manual break is needed here
                    bp = BreakpointNode.break_(lg_long)
                    LongAction()
        # BreakpointNode is special, that entering it does not automatically link to the current active node.
        # But it does make itself active one.
        # So we are not expecting to see a `TooManyChildrenError` here, which will raise if any other LogicNode entered here (RootLogicNode only allows 1 child).
        # This is totally valid and recommended to build a complex tree, part by part.
        with bp:
            with LogicGroup(name='OpenCheck.Short'):
                with lg.predicted_return < -0.03:
                    with lg.risk_level < 1:
                        ShortAction()
# Expected to see a real linkage from the breakpoint, to the `lg.predicted_return < -0.03` node.
# As the breakpoint is the only parent of the connected branch.
# if multiple breakpoint exist, the rest will show as virtual linkage.
# check the node info panel (press space or hover) to see the node labels.
# the `lg.predicted_return < -0.03` does not belong to OuterOpenCheck.Long LogicGroup, even it connects to a child of the group
root.show(with_eval=False)
