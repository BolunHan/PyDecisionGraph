import unittest

HAS_CEXT = True
IMPORT_ERR = None
try:
    # Prefer the Cython-compiled extension as requested
    from decision_graph.decision_tree.c_abc import LogicExpression  # type: ignore
except Exception as e:  # pragma: no cover
    HAS_CEXT = False
    IMPORT_ERR = e


@unittest.skipUnless(HAS_CEXT, f"c_abc extension not available: {IMPORT_ERR}")
class TestLogicExpression(unittest.TestCase):
    # === eval ===
    def test_eval_literal_bool(self):
        le_true = LogicExpression(True, bool)
        le_false = LogicExpression(False, bool)
        self.assertTrue(le_true.eval())
        self.assertFalse(le_false.eval())
        self.assertTrue(bool(le_true))
        self.assertFalse(bool(le_false))

    def test_eval_literal_int_and_enforce_dtype(self):
        le = LogicExpression(5, int)
        self.assertEqual(le.eval(), 5)
        # enforce dtype shouldn't change a matching type
        self.assertEqual(le.eval(enforce_dtype=True), 5)

    def test_eval_callable(self):
        le = LogicExpression(lambda: 2 + 3, int)
        self.assertEqual(le.eval(), 5)
        self.assertEqual(le.eval(enforce_dtype=True), 5)

    def test_eval_raises_exception(self):
        with self.assertRaises(ValueError):
            LogicExpression(ValueError("boom"), object).eval()

    # === cast ===
    def test_cast_from_int(self):
        le = LogicExpression.cast(10)
        self.assertIsInstance(le, LogicExpression)
        self.assertEqual(le.eval(), 10)
        rep = repr(le)
        self.assertIn("dtype=int", rep)
        self.assertIn("repr=10", rep)

    def test_cast_from_bool(self):
        le = LogicExpression.cast(True)
        self.assertTrue(le.eval())
        self.assertTrue(bool(le))

    def test_cast_from_callable(self):
        def f():
            return 42
        le = LogicExpression.cast(f)
        self.assertEqual(le.eval(), 42)
        rep = repr(le)
        self.assertIn("LogicExpression(", rep)

    def test_cast_idempotent(self):
        le = LogicExpression.cast(7)
        le2 = LogicExpression.cast(le)
        self.assertIs(le2, le)

    def test_cast_from_exception(self):
        err = RuntimeError("boom")
        le = LogicExpression.cast(err)
        with self.assertRaises(RuntimeError):
            le.eval()

    # === dunder boolean logic ===
    def test_and_or(self):
        a = LogicExpression.cast(True, bool)
        b = LogicExpression.cast(False, bool)
        self.assertFalse((a & b).eval())
        self.assertTrue((a | b).eval())

    def test_eq(self):
        a = LogicExpression.cast(3)
        self.assertTrue((a == 3).eval())
        self.assertTrue((a == LogicExpression.cast(3)).eval())
        self.assertFalse((a == 4).eval())

    # === arithmetic dunders ===
    def test_add_sub_mul(self):
        a = LogicExpression.cast(5)
        self.assertEqual((a + 2).eval(), 7)
        self.assertEqual((a - 3).eval(), 2)
        self.assertEqual((a * 4).eval(), 20)

    def test_divisions(self):
        a = LogicExpression.cast(5)
        self.assertEqual((a // 2).eval(), 2)
        self.assertEqual((a / 2).eval(), 2.5)

    def test_pow(self):
        a = LogicExpression.cast(2)
        self.assertEqual((a ** 3).eval(), 8)

    # === comparison dunders ===
    def test_comparisons(self):
        a = LogicExpression.cast(5)
        self.assertTrue((a < 6).eval())
        self.assertTrue((a <= 5).eval())
        self.assertTrue((a > 4).eval())
        self.assertTrue((a >= 5).eval())
        self.assertFalse((a < 5).eval())
        self.assertFalse((a > 5).eval())


if __name__ == "__main__":  # pragma: no cover
    unittest.main()

