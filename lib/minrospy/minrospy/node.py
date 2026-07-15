"""Node — yüksek seviye tipli API (RawNode + reliability.Reliable sarmalayıcı).

Subscriber callback'leri doğrudan tipli mesaj alır, deserializasyon otomatiktir.
İçte bir RawNode (saf ham byte) ve bir Reliable (overlay) tutar; C++ Node'in portu.
Reliable retransmit otonomdur — retransmit_cb gerekmez.

Kullanım:
    node = Node()
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
from .logging.logger import Level, Logger
from .reliability.reliable import Reliable
from .raw_node import RawNode, Transport


class Publisher:
    """create_publisher() tarafından döndürülür; veri tutmaz (Reliable tutar)."""

    def __init__(self, hl: "Node", msg_type: type, ch_id: int, reliable: bool):
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


class Node:
    def __init__(
        self,
        max_frame_data: int = wireframe.MAX_DATA_LEN,
        max_retry: int = 3,
        timeout_ms: int = 50,
    ):
        self._node = RawNode(max_frame_data)
        self._reliable = Reliable(self._node, max_retry=max_retry, timeout_ms=timeout_ms)
        # Logger yalnızca PUBLISH eder (sink değil) -> broker subscriber slotu
        # tüketmez. Log ALMAK için host tarafında logging.LogSink kullanılır.
        self._logger = Logger(self._node, frame_data=max_frame_data)

    # Log seviyeleri (logging.Level takma adı).
    LogLevel = Level

    # transport doğrudan alttaki RawNode'a yönlendirilir
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

    # ── Logging (best-effort, CH248) ──────────────────────────────────────
    # Yalnızca yayın. min_level altındaki çağrılar wire'a hiç dokunmaz. Uzun
    # mesaj otomatik parçalanır. Log ALMAK için logging.LogSink kullanın.

    def set_log_level(self, level: Level) -> None:
        """Eşik seviyesi: bu seviyenin altındaki loglar bastırılır."""
        self._logger.set_min_level(level)

    def log(self, level: Level, msg) -> None:
        """Ham byte / str log."""
        self._logger.log(level, msg)

    def log_debug(self, msg) -> None:
        self._logger.debug(msg)

    def log_info(self, msg) -> None:
        self._logger.info(msg)

    def log_warn(self, msg) -> None:
        self._logger.warn(msg)

    def log_error(self, msg) -> None:
        self._logger.error(msg)

    def log_fatal(self, msg) -> None:
        self._logger.fatal(msg)
