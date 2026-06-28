"""NodeHL — yüksek seviye tipli API (Node + reliability.Reliable sarmalayıcı).

Subscriber callback'leri doğrudan tipli mesaj alır, deserializasyon otomatiktir.
İçte bir Node (saf ham byte) ve bir Reliable (overlay) tutar; C++ NodeHL'in portu.
Reliable retransmit otonomdur — retransmit_cb gerekmez.

Kullanım:
    node = NodeHL()
    node.transport = Transport(...)

    # Publisher
    pub = node.create_publisher(Twist, ch_id)
    pub.publish(msg)

    # Subscriber — callback tipli mesaj alır
    node.create_subscription(Twist, ch_id, lambda msg: ...)

    # Güvenilir publisher — buffer'ı Reliable kendi içinde tutar
    pub = node.create_publisher(Twist, ch_id, reliable=True)
    if not pub.publish(msg):
        ...  # önceki hâlâ uçuşta (ACK bekleniyor), sonra dene

    # Güvenilir subscriber
    node.create_subscription(Twist, ch_id, lambda msg: ..., reliable=True)

    node.spin_once()   # ana döngüde
"""

from collections.abc import Callable

from .core import wireframe
from .node import Node, Transport
from .reliability.reliable import Reliable


class Publisher:
    """create_publisher() tarafından döndürülür; veri tutmaz (Reliable tutar)."""

    def __init__(self, hl: "NodeHL", msg_type: type, ch_id: int, reliable: bool):
        self._hl = hl
        self._msg_type = msg_type
        self._ch_id = ch_id
        self._reliable = reliable

    def publish(self, msg) -> bool:
        if self._hl is None:
            return False
        buf = msg.to_bytes()
        if self._reliable:
            return self._hl._reliable.publish(self._ch_id, buf)
        return self._hl._node.publish(self._ch_id, buf)

    def is_valid(self) -> bool:
        return self._hl is not None


class NodeHL:
    def __init__(
        self,
        max_frame_data: int = wireframe.MAX_DATA_LEN,
        max_retry: int = 3,
        timeout_ms: int = 50,
    ):
        self._node = Node(max_frame_data)
        self._reliable = Reliable(self._node, max_retry=max_retry, timeout_ms=timeout_ms)

    # transport doğrudan alttaki Node'a yönlendirilir
    @property
    def transport(self) -> Transport:
        return self._node.transport

    @transport.setter
    def transport(self, t: Transport) -> None:
        self._node.transport = t

    def spin_once(self) -> None:
        self._node.spin_once()
        self._reliable.tick()

    def create_publisher(
        self,
        msg_type: type,
        ch_id: int,
        reliable: bool = False,
    ) -> Publisher | None:
        """reliable=True ise güvenilir publisher kanalı kaydedilir.

        Retransmit otonomdur — kullanıcı callback'i gerekmez. Slot doluysa
        (MAX_PUB aşıldıysa) None döner.
        """
        if reliable and not self._reliable.register_pub(ch_id):
            return None
        return Publisher(self, msg_type, ch_id, reliable)

    def create_subscription(
        self,
        msg_type: type,
        ch_id: int,
        cb: Callable[[object], None],
        reliable: bool = False,
    ) -> bool:
        """cb imzası: fn(msg) — msg, msg_type örneğidir."""
        if cb is None:
            return False

        def adapter(payload: bytes) -> None:
            msg = msg_type.from_bytes(payload)
            if msg is None:
                return
            cb(msg)

        if reliable:
            return self._reliable.subscribe(ch_id, adapter)
        return self._node.subscribe(ch_id, adapter)
