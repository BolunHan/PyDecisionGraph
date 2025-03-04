__package__ = 'decision_graph.demo'


def node_test():
    from decision_graph.decision_tree import LogicNode, LOGGER, AttrExpression, LongAction, ShortAction, NoAction, RootLogicNode, LogicMapping

    LogicMapping.AttrExpression = AttrExpression
    state = {
        "exposure": 0,
        "working_order": 0,
        "up_prob": 0.8,
        "down_prob": 0.2,
        "volatility": 0.24,
        "ttl": 15.3
    }

    # with RootLogicNode() as root:
    #     with LogicMapping(name='Root', data=state) as lg_root:
    #         lg_root: LogicMapping
    #
    #         with lg_root.volatility > 0.25:
    #             with lg_root.down_prob > 0.2:
    #                 LongAction()
    #
    #             with lg_root.down_prob > 0.1:
    #                 LongAction()
    #
    #             with lg_root.up_prob < -0.1:
    #                 ShortAction()
    #
    #             with lg_root.up_prob < -0.2:
    #                 ShortAction()

    with RootLogicNode() as root:
        with LogicMapping(name='Root', data=state) as lg_root:
            lg_root: LogicMapping

            with lg_root.exposure == 0:
                root: LogicNode
                with LogicMapping(name='check_open', data=state) as lg:
                    with lg.working_order != 0:
                        break_point = NoAction()
                        lg.break_(scope=lg)

                    with lg.volatility > 0.25:
                        with lg.down_prob > 0.1:
                            LongAction()

                        with lg.up_prob < -0.1:
                            ShortAction()

            with lg_root.ttl > 30:
                with lg_root.working_order > 0:
                    ShortAction()
                LongAction()
                lg_root.break_(scope=lg_root)

            with LogicMapping(name='check_close', data=state) as lg:
                with (lg.exposure > 0) & (lg.down_prob > 0.):
                    ShortAction()

                with (lg.exposure < 0) & (lg.up_prob > 0.):
                    LongAction()

    root.to_html()
    LOGGER.info(root())


def main():
    # skippable_context_test()
    node_test()


if __name__ == '__main__':
    main()
