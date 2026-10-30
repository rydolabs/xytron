import sys
import types
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import xytron


class ExportsTests(unittest.TestCase):
    def test_explicit_decorators_and_public_class_members(self):
        local = types.ModuleType("test_exports")
        @xytron.export
        def add(a: int, b: int) -> int:
            return a + b
        @xytron.export
        class Player:
            def __init__(self):
                self._hp = 100
            @xytron.export
            def move(self, x: float, y: float):
                return (x, y)
            def _secret(self):
                raise RuntimeError("Not exported")
            @property
            @xytron.export
            def hp(self):
                return self._hp
        Player.unmarked = lambda self: None
        local.add = add
        local.Player = Player
        local.secret = lambda: 99
        found = xytron.discover_exports(local)
        self.assertEqual(set(found), {"add", "Player"})
        self.assertEqual(found["Player"]["methods"], ["move"])
        self.assertEqual(found["Player"]["properties"], ["hp"])
        self.assertEqual(add(20, 22), 42)

    def test_reject_private_and_invalid_exports(self):
        with self.assertRaises(TypeError):
            xytron.export(42)
        def _private():
            pass
        with self.assertRaises(ValueError):
            xytron.export(_private)

    def test_alias_is_not_implicitly_exported(self):
        local = types.ModuleType("test_alias")
        @xytron.export
        def original():
            return 1
        local.original = original
        local.alias = original
        self.assertEqual(list(xytron.discover_exports(local)), ["original"])


if __name__ == "__main__":
    unittest.main()
