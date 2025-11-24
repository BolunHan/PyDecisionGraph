import unittest
from decision_graph.decision_tree.capi import c_node, c_abc, c_collection, LGM
from decision_graph.decision_tree.exc import TooManyChildren, TooFewChildren


class TestRootLogicNode(unittest.TestCase):
    def test_append_and_child(self):
        LGM.clear()
        root = c_node.RootLogicNode()
        child = c_abc.LogicNode()
        root.append(child)
        self.assertEqual(root.child, child)

    def test_build_child(self):
        LGM.clear()
        with c_node.RootLogicNode() as root:
            with c_abc.LogicNode() as child:
                pass
        self.assertEqual(root.child, child)

    def test_append_too_many_children(self):
        LGM.clear()
        root = c_node.RootLogicNode()
        child1 = c_abc.LogicNode()
        child2 = c_abc.LogicNode()
        root.append(child1)
        with self.assertRaises(TooManyChildren):  # TooManyChildren
            root.append(child2)

    def test_append_too_many_children(self):
        LGM.clear()
        with self.assertRaises(TooManyChildren):  # TooManyChildren
            with c_node.RootLogicNode() as root:
                with c_abc.LogicNode() as child1:
                    pass
                with c_abc.LogicNode() as child2:
                    pass

    def test_child_no_child(self):
        LGM.clear()
        root = c_node.RootLogicNode()
        with self.assertRaises(TooFewChildren):
            _ = root.child


class TestContextLogicExpression(unittest.TestCase):
    def setUp(self):
        self.lm = c_collection.LogicMapping(name="test", data={"a": 1, "b": {"c": 2}})
        self.lm.__enter__()

    def tearDown(self):
        self.lm.__exit__(None, None, None)

    def test_init(self):
        expr = c_node.ContextLogicExpression(logic_group=self.lm)
        self.assertEqual(expr.logic_group, self.lm)

    def test_getitem(self):
        expr = c_node.ContextLogicExpression(logic_group=self.lm)
        attr_expr = expr["a"]
        self.assertIsInstance(attr_expr, c_node.AttrExpression)
        self.assertEqual(attr_expr.attr, "a")

    def test_getattr(self):
        expr = c_node.ContextLogicExpression(logic_group=self.lm)
        attr_expr = expr.a
        self.assertIsInstance(attr_expr, c_node.AttrExpression)
        self.assertEqual(attr_expr.attr, "a")

    def test_add(self):
        expr = c_node.ContextLogicExpression(logic_group=self.lm)
        math_expr = (left := expr.a) + 1
        self.assertIsInstance(math_expr, c_node.MathExpression)
        self.assertIs(math_expr.left, left)
        self.assertEqual(math_expr.right, 1)
        self.assertEqual(math_expr.op_name, "add")

    def test_eq(self):
        expr = c_node.ContextLogicExpression(logic_group=self.lm)
        comp_expr = (left := expr.a) == 1
        self.assertIsInstance(comp_expr, c_node.ComparisonExpression)
        self.assertIs(comp_expr.left, left)
        self.assertEqual(comp_expr.right, 1)
        self.assertEqual(comp_expr.op_name, "eq")

    def test_and(self):
        expr = c_node.ContextLogicExpression(logic_group=self.lm)
        log_expr = (left := expr.a) & (right := expr.b)
        self.assertIsInstance(log_expr, c_node.LogicalExpression)
        self.assertIs(log_expr.left, left)
        self.assertIs(log_expr.right, right)
        self.assertEqual(log_expr.op_name, "and_")

    def test_neg(self):
        expr = c_node.ContextLogicExpression(logic_group=self.lm)
        math_expr = -(left := expr.a)
        self.assertIsInstance(math_expr, c_node.MathExpression)
        self.assertIs(math_expr.left, left)
        self.assertEqual(math_expr.op_name, "neg")

    def test_invert(self):
        expr = c_node.ContextLogicExpression(logic_group=self.lm)
        log_expr = ~(left := expr.a)
        self.assertIsInstance(log_expr, c_node.LogicalExpression)
        self.assertIs(log_expr.left, left)
        self.assertEqual(log_expr.op_name, "not_")


class TestAttrExpression(unittest.TestCase):
    def setUp(self):
        self.lm = c_collection.LogicMapping(name="test", data={"a": 1, "b": {"c": 2}})
        self.lm.__enter__()

    def tearDown(self):
        self.lm.__exit__(None, None, None)

    def test_init(self):
        attr_expr = c_node.AttrExpression(attr="a")
        self.assertEqual(attr_expr.attr, "a")

    def test_getitem(self):
        attr_expr = c_node.AttrExpression(attr="b")
        nested = attr_expr["c"]
        self.assertIsInstance(nested, c_node.AttrNestedExpression)
        self.assertEqual(nested.attrs, ["b", "c"])

    def test_getattr(self):
        attr_expr = c_node.AttrExpression(attr="b")
        nested = attr_expr.c
        self.assertIsInstance(nested, c_node.AttrNestedExpression)
        self.assertEqual(nested.attrs, ["b", "c"])


class TestAttrNestedExpression(unittest.TestCase):
    def setUp(self):
        self.lm = c_collection.LogicMapping(name="test", data={"a": 1, "b": {"c": 2}})
        self.lm.__enter__()

    def tearDown(self):
        self.lm.__exit__(None, None, None)

    def test_init(self):
        nested = c_node.AttrNestedExpression(attrs=["b", "c"])
        self.assertEqual(nested.attrs, ["b", "c"])

    def test_getitem(self):
        nested = c_node.AttrNestedExpression(attrs=["b"])
        deeper = nested["d"]
        self.assertIsInstance(deeper, c_node.AttrNestedExpression)
        self.assertEqual(deeper.attrs, ["b", "d"])

    def test_getattr(self):
        nested = c_node.AttrNestedExpression(attrs=["b"])
        deeper = nested.d
        self.assertIsInstance(deeper, c_node.AttrNestedExpression)
        self.assertEqual(deeper.attrs, ["b", "d"])
        self.assertEqual(nested(), {'c': 2})
        with self.assertRaises(KeyError):
            deeper()


class TestMathExpression(unittest.TestCase):
    def setUp(self):
        self.lm = c_collection.LogicMapping(name="test", data={"a": 1, "b": 2})
        self.lm.__enter__()

    def tearDown(self):
        self.lm.__exit__(None, None, None)

    def test_init(self):
        expr = c_node.MathExpression(left=self.lm.a, op="add", right=1)
        self.assertEqual(expr.left, self.lm.a)
        self.assertEqual(expr.right, 1)
        self.assertEqual(expr.op_name, "add")

    def test_eval(self):
        expr = c_node.MathExpression(left=self.lm.a, op="add", right=1)
        result = expr.eval()
        self.assertEqual(result, 2)


class TestComparisonExpression(unittest.TestCase):
    def setUp(self):
        self.lm = c_collection.LogicMapping(name="test", data={"a": 1, "b": 2})
        self.lm.__enter__()

    def tearDown(self):
        self.lm.__exit__(None, None, None)

    def test_init(self):
        expr = c_node.ComparisonExpression(left=self.lm.a, op="eq", right=1)
        self.assertEqual(expr.left, self.lm.a)
        self.assertEqual(expr.right, 1)
        self.assertEqual(expr.op_name, "eq")

    def test_eval(self):
        expr = c_node.ComparisonExpression(left=self.lm.a, op="eq", right=1)
        result = expr.eval()
        self.assertTrue(result)

    def test_lt(self):
        expr = c_node.ComparisonExpression(left=self.lm.a, op="lt", right=2)
        result = expr.eval()
        self.assertTrue(result)

    def test_le(self):
        expr = c_node.ComparisonExpression(left=self.lm.a, op="le", right=1)
        result = expr.eval()
        self.assertTrue(result)

    def test_gt(self):
        expr = c_node.ComparisonExpression(left=self.lm.b, op="gt", right=1)
        result = expr.eval()
        self.assertTrue(result)

    def test_ge(self):
        expr = c_node.ComparisonExpression(left=self.lm.b, op="ge", right=2)
        result = expr.eval()
        self.assertTrue(result)

    def test_ne(self):
        expr = c_node.ComparisonExpression(left=self.lm.a, op="ne", right=2)
        result = expr.eval()
        self.assertTrue(result)

    def test_eq_false(self):
        expr = c_node.ComparisonExpression(left=self.lm.a, op="eq", right=2)
        result = expr.eval()
        self.assertFalse(result)


class TestLogicalExpression(unittest.TestCase):
    def setUp(self):
        self.lm = c_collection.LogicMapping(name="test", data={"a": True, "b": False})
        self.lm.__enter__()

    def tearDown(self):
        self.lm.__exit__(None, None, None)

    def test_init(self):
        expr = c_node.LogicalExpression(left=self.lm.a, op="and_", right=self.lm.b)
        self.assertEqual(expr.left, self.lm.a)
        self.assertEqual(expr.right, self.lm.b)
        self.assertEqual(expr.op_name, "and_")

    def test_eval(self):
        expr = c_node.LogicalExpression(left=self.lm.a, op="and_", right=self.lm.b)
        result = expr.eval()
        self.assertFalse(result)


class TestMathExpressionOperator(unittest.TestCase):
    def test_from_str(self):
        op = c_node.MathExpressionOperator.from_str("add")
        self.assertEqual(op.name, "add")

    def test_to_func(self):
        op = c_node.MathExpressionOperator.add
        func = op.to_func()
        self.assertEqual(func(1, 2), 3)


class TestComparisonExpressionOperator(unittest.TestCase):
    def test_from_str(self):
        op = c_node.ComparisonExpressionOperator.from_str("eq")
        self.assertEqual(op.name, "eq")

    def test_to_func(self):
        op = c_node.ComparisonExpressionOperator.eq
        func = op.to_func()
        self.assertTrue(func(1, 1))


class TestLogicalExpressionOperator(unittest.TestCase):
    def test_from_str(self):
        op = c_node.LogicalExpressionOperator.from_str("and_")
        self.assertEqual(op.name, "and_")

    def test_to_func(self):
        op = c_node.LogicalExpressionOperator.and_
        func = op.to_func()
        self.assertFalse(func(True, False))


if __name__ == "__main__":
    unittest.main()
