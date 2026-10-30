from xytron import export


@export
class Player:
    def __init__(self, name: str):
        self.name = name
        self.hp = 100

    @export
    def hit(self, amount: int) -> int:
        print("PLUGIN_LOG")
        self.hp -= amount
        return self.hp

    @property
    @export
    def health(self) -> int:
        return self.hp

    @health.setter
    def health(self, value: int) -> None:
        self.hp = value

    def secret(self):
        return 'not available'

    def close(self):
        import sys
        print("CLOSED_PYTHON", file=sys.stderr)


class Unmarked:
    pass
