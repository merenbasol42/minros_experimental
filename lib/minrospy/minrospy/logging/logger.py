"""logging — minros log overlay'i (Python portu).

reliability gibi bağımsız bir overlay: core'a hiçbir şey eklemez, yalnızca
RawNode'un public publish/subscribe API'sini kullanır. Log seviyesi + parça
bilgisini payload'ın önüne 1 baytlık opak FLAGS öneki olarak koyar ve rezerve
LOG kanalından (CH248) yayar. C++ minros::logging portudur.

İki ayrı sınıf — publisher ve sink ayrıdır (C++ ile simetrik):

    Logger  : yayıncı. Kaynakta seviye filtresi; uzun mesaj otomatik parçalanır.
              Pratikte gömülü slave yayınlar, ama Python tarafı da yayabilir.

    LogSink : dinleyici/reassembler. Parçaları birleştirir; kayıp parçayı SEQ4
              sürekliliğiyle tespit edip bozuk satır üretmez (kanal best-effort).
              Host'un (bu Python tarafı) asıl kullandığı sınıf.

Kullanım (host sink):
    node = RawNode()          # veya Node
    sink = LogSink(node)
    sink.subscribe(lambda level, msg: print(level.name, msg.decode(errors="replace")))
    while True:
        node.spin_once()

Kullanım (yayın):
    log = Logger(node)
    log.set_min_level(Level.INFO)
    log.info("motor started")
"""

from collections.abc import Callable

from ..core import wireframe
from . import protocol
from .protocol import Level

# fn(level, msg_bytes) — birleştirilmiş tam satır, FLAGS ayıklanmış
LogCallback = Callable[[Level, bytes], None]


class Logger:
    """Log yayıncısı. Kaynakta min_level filtresi + otomatik parçalama."""

    def __init__(self, node, frame_data: int = wireframe.MAX_DATA_LEN):
        self._node = node
        # Bir parçada taşınabilen text: frame_data - CH_ID(1) - FLAGS(1).
        self._chunk = frame_data - 2
        if self._chunk < 1:
            raise ValueError("frame_data en az 3 olmalı (CH_ID + FLAGS + >=1 text)")
        self._min = Level.DEBUG

    def set_min_level(self, level: Level) -> None:
        """Eşik seviyesi: bu seviyenin altındaki loglar bastırılır."""
        self._min = level

    @property
    def min_level(self) -> Level:
        return self._min

    def log(self, level: Level, msg) -> None:
        """Ham byte / str log. Uzunsa CHUNK'lara bölünüp SEQ4 ile numaralanır."""
        if int(level) < int(self._min):
            return
        if isinstance(msg, str):
            msg = msg.encode()
        data = bytes(msg)

        off = 0
        seq = 0
        n = len(data)
        while True:
            end = min(off + self._chunk, n)
            last = end == n
            flags = protocol.pack_flags(level, seq, last)
            self._node.publish(
                protocol.LOG_CHANNEL_ID, data[off:end], head=bytes((flags,))
            )
            off = end
            seq = (seq + 1) & 0x0F
            if off >= n:  # n==0 dahil: tek boş parça yayılır
                break

    def debug(self, msg) -> None:
        self.log(Level.DEBUG, msg)

    def info(self, msg) -> None:
        self.log(Level.INFO, msg)

    def warn(self, msg) -> None:
        self.log(Level.WARN, msg)

    def error(self, msg) -> None:
        self.log(Level.ERROR, msg)

    def fatal(self, msg) -> None:
        self.log(Level.FATAL, msg)


class LogSink:
    """Log dinleyici. Parçaları birleştirir, kayıp parçada satırı atar."""

    def __init__(self, node):
        self._node = node
        self._cb: LogCallback | None = None
        self._buf = bytearray()
        self._expect_seq = 0
        self._level = Level.DEBUG
        self._in_msg = False
        self._dropped = 0

    def subscribe(self, cb: LogCallback) -> bool:
        """cb imzası: fn(level, msg_bytes) — birleştirilmiş tam satır."""
        self._cb = cb
        return self._node.subscribe(protocol.LOG_CHANNEL_ID, self._rx)

    @property
    def dropped(self) -> int:
        """Kaybedilen (atılan) log sayacı — teşhis için."""
        return self._dropped

    def _reset(self) -> None:
        self._in_msg = False
        self._buf.clear()

    def _start(self, level: Level) -> None:
        self._in_msg = True
        self._buf.clear()
        self._expect_seq = 0
        self._level = level

    def _rx(self, payload: bytes) -> None:
        if len(payload) < 1:
            return
        flags = payload[0]
        last = protocol.flag_last(flags)
        level = protocol.flag_level(flags)
        seq = protocol.flag_seq(flags)
        data = payload[1:]

        # Süreklilik: mesaj seq0 ile başlar; sonraki parçalar expect_seq takip eder.
        if not self._in_msg or seq != self._expect_seq:
            if self._in_msg:
                self._dropped += 1  # yarım kalmış satır -> at
            if seq != 0:  # ortadan yakalandık
                self._reset()
                self._dropped += 1
                return
            self._start(level)

        self._buf += data
        self._expect_seq = (seq + 1) & 0x0F

        if last:
            if self._cb is not None:
                self._cb(self._level, bytes(self._buf))
            self._reset()
