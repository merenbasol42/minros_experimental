"""parameters overlay testi — ParamServer <-> ParamClient bellek-içi loopback."""

from minrospy import Node, RawNode, Transport
from minrospy.interfaces.std_msgs import Float32, PidGains
from minrospy.overlays.parameters import ParamClient, ParamServer
from minrospy.overlays.parameters import protocol as pp


class Fifo:
    def __init__(self):
        self.buf = bytearray()

    def send(self, data: bytes):
        self.buf.extend(data)

    def read(self, n: int) -> bytes:
        n = min(n, len(self.buf))
        out = bytes(self.buf[:n])
        del self.buf[:n]
        return out

    def size(self) -> int:
        return len(self.buf)


def _link():
    h2d, d2h = Fifo(), Fifo()
    dev, host = RawNode(), RawNode()
    dev.transport = Transport(d2h.send, h2d.read, h2d.size, lambda: 0)
    host.transport = Transport(h2d.send, d2h.read, d2h.size, lambda: 0)
    return dev, host


def _pump(dev, host, n=4):
    for _ in range(n):
        dev.spin_once()
        host.spin_once()


def test_get_set_roundtrip():
    dev, host = _link()
    server = ParamServer(dev)
    server.register_param(5, Float32(0.0))
    client = ParamClient(host)

    # GET → 0.0
    client.get(5)
    _pump(dev, host)
    pid, msg = client.last_value
    assert pid == 5 and isinstance(msg, Float32) and msg.value == 0.0

    # SET 2.5 → onay + storage
    client.set(5, Float32(2.5))
    _pump(dev, host)
    pid, msg = client.last_value
    assert msg.value == 2.5
    assert server.value(5).value == 2.5


def test_unknown_id():
    dev, host = _link()
    ParamServer(dev)  # boş registry
    client = ParamClient(host)
    client.get(9)
    _pump(dev, host)
    assert client.last_error == (9, pp.ErrCode.UNKNOWN_ID)


def test_type_mismatch():
    dev, host = _link()
    server = ParamServer(dev)
    server.register_param(5, Float32(0.0))
    client = ParamClient(host)
    # PidGains tanımlayıcısıyla Float32 parametresine SET
    client.set(5, PidGains(1.0, 2.0, 3.0))
    _pump(dev, host)
    assert client.last_error == (5, pp.ErrCode.TYPE_MISMATCH)


def test_read_only():
    dev, host = _link()
    server = ParamServer(dev)
    server.register_param(6, Float32(1.0), read_only=True)
    client = ParamClient(host)
    client.set(6, Float32(9.0))
    _pump(dev, host)
    assert client.last_error == (6, pp.ErrCode.READ_ONLY)
    assert server.value(6).value == 1.0  # değişmedi


def test_node_facade_integration():
    # Device: yüksek seviye Node (ParamServer facade içinde). Host: RawNode + client.
    h2d, d2h = Fifo(), Fifo()
    dev, host = Node(), RawNode()
    dev.transport = Transport(d2h.send, h2d.read, h2d.size, lambda: 0)
    host.transport = Transport(h2d.send, d2h.read, d2h.size, lambda: 0)

    kp = Float32(0.0)
    dev.register_param(5, kp)
    client = ParamClient(host)

    client.set(5, Float32(4.5))
    for _ in range(4):
        dev.spin_once()
        host.spin_once()
    assert dev.param_value(5).value == 4.5
    assert client.last_value[1].value == 4.5


def test_composite_atomic():
    dev, host = _link()
    server = ParamServer(dev)
    server.register_param(7, PidGains())
    client = ParamClient(host)
    client.set(7, PidGains(1.0, 2.0, 3.0))
    _pump(dev, host)
    got = server.value(7)
    assert (got.kp, got.ki, got.kd) == (1.0, 2.0, 3.0)
    pid, msg = client.last_value
    assert isinstance(msg, PidGains) and msg.kd == 3.0
