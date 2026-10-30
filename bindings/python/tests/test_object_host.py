"""Tests the Python managed object host, not a browser transport."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from xytron import export
from xytron.object_host import ObjectHost, ObjectHostError


@export
class Player:
    closed = 0

    def __init__(self, name: str):
        self.name = name
        self._health = 100

    @export
    def attack(self, amount: int) -> int:
        self._health -= amount
        return self._health

    def secret(self) -> str:
        return 'private'

    @property
    @export
    def health(self) -> int:
        return self._health

    @health.setter
    def health(self, value: int) -> None:
        self._health = value

    def close(self):
        Player.closed += 1


class HostTests(unittest.TestCase):
    def setUp(self):
        Player.closed = 0
        self.host = ObjectHost('example_py', [Player],
                               authorize=lambda owner, operation: owner != 'blocked')

    def test_create_method_property_and_dispose(self):
        h = self.host.invoke('frame-1', 'example_py.Player.create', 'Jane')
        self.assertTrue(h.startswith('xy_'))
        self.assertEqual(self.host.invoke('frame-1', 'example_py.Player.method.attack', h, 9), 91)
        self.assertEqual(self.host.invoke('frame-1', 'example_py.Player.get.health', h), 91)
        self.host.invoke('frame-1', 'example_py.Player.set.health', h, 17)
        self.assertEqual(self.host.invoke('frame-1', 'example_py.Player.get.health', h), 17)
        self.assertIsNone(self.host.invoke('frame-1', 'example_py.Player.dispose', h))
        self.assertEqual(Player.closed, 1)
        self.assertEqual(self.host.live_count, 0)
        with self.assertRaises(ObjectHostError):
            self.host.invoke('frame-1', 'example_py.Player.dispose', h)

    def test_unknown_owner_and_unexported_members(self):
        h = self.host.invoke('frame-1', 'example_py.Player.create', 'Jane')
        for owner, path in [('frame-2', 'example_py.Player.method.attack'),
                            ('frame-1', 'example_py.Player.method.secret'),
                            ('frame-1', 'example_py.Player.method.__class__'),
                            ('frame-1', 'example_py.Player.get.name'),
                            ('frame-1', 'other.Player.method.attack')]:
            with self.subTest(owner=owner, path=path), self.assertRaises(ObjectHostError):
                self.host.invoke(owner, path, h, 1)
        self.assertEqual(self.host.live_count, 1)

    def test_authorization_and_type_checks(self):
        with self.assertRaises(ObjectHostError):
            self.host.invoke('blocked', 'example_py.Player.create', 'Jane')
        with self.assertRaises(ObjectHostError):
            self.host.invoke('frame-1', 'example_py.Player.create', 22)
        h = self.host.invoke('frame-1', 'example_py.Player.create', 'Jane')
        with self.assertRaises(ObjectHostError):
            self.host.invoke('frame-1', 'example_py.Player.method.attack', h, True)
        with self.assertRaises(ObjectHostError):
            self.host.invoke('frame-1', 'example_py.Player.method.attack', h, 2**54)
        with self.assertRaises(ObjectHostError):
            self.host.invoke('frame-1', 'example_py.Player.set.health', h, 'bad')
        self.assertEqual(self.host.invoke('frame-1', 'example_py.Player.get.health', h), 100)

    def test_disconnect_cleanup_and_limit(self):
        host = ObjectHost('example_py', [Player], lambda owner, op: True, max_objects=2)
        a = host.invoke('one', 'example_py.Player.create', 'A')
        b = host.invoke('two', 'example_py.Player.create', 'B')
        with self.assertRaises(ObjectHostError):
            host.invoke('one', 'example_py.Player.create', 'C')
        self.assertEqual(host.cleanup('one'), 1)
        self.assertEqual(Player.closed, 1)
        with self.assertRaises(ObjectHostError):
            host.invoke('one', 'example_py.Player.get.health', a)
        self.assertEqual(host.invoke('two', 'example_py.Player.get.health', b), 100)
        self.assertEqual(host.cleanup('two'), 1)
        self.assertEqual(host.live_count, 0)

    def test_reject_unmarked_class_and_default_denial(self):
        class Undecorated:
            pass
        with self.assertRaises(ValueError):
            ObjectHost('example_py', [Undecorated], lambda owner, op: True)
        host = ObjectHost('example_py', [Player], lambda owner, op: False)
        with self.assertRaises(ObjectHostError):
            host.invoke('frame-1', 'example_py.Player.create', 'Jane')


if __name__ == '__main__':
    unittest.main()
