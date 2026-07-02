"""Parser: gelen byte akışını frame'lere ayrıştıran durum makinesi.

Checksum: CRC-8/SMBUS (polinom 0x07, init 0x00) — wireframe ile paylaşılır.

Kullanım:
    parser = Parser()
    parser.on_frame_completed = lambda data: ...   # data: bytes (CH_ID + PAYLOAD)
    parser.on_error           = lambda err: ...
    parser.feed(incoming_bytes)
"""

import enum
from collections.abc import Callable

from . import wireframe


class Error(enum.Enum):
    INVALID_LENGTH = 0  # length alanı MIN_DATA_LEN'den küçük veya MAX_DATA sınırını aşıyor
    CRC_MISMATCH = 1    # alınan CRC hesaplanan CRC ile eşleşmiyor


class _State(enum.Enum):
    HEADER_WAIT = 0
    LENGTH_WAIT = 1
    DATA_READING = 2
    CRC_WAIT = 3


class Parser:
    def __init__(self, max_data: int = wireframe.MAX_DATA_LEN):
        if not (wireframe.MIN_DATA_LEN <= max_data <= wireframe.MAX_DATA_LEN):
            raise ValueError("max_data: MIN_DATA_LEN..MAX_DATA_LEN aralığında olmalı")
        self.max_data = max_data

        # data: bytes (CH_ID + PAYLOAD)
        self.on_frame_completed: Callable[[bytes], None] | None = None
        self.on_error: Callable[[Error], None] | None = None

        self._reset()

    def feed(self, data: bytes) -> None:
        """Gelen byte'ları durum makinesine işler; tam frame'lerde callback tetiklenir."""
        for byte in data:
            self._advance(byte)

    # ── İç durum makinesi ──────────────────────────────────────────────────

    def _advance(self, byte: int) -> None:
        if self._state is _State.HEADER_WAIT:
            if byte == wireframe.HEADER[self._header_matched]:
                self._header_matched += 1
                if self._header_matched == wireframe.HEADER_SIZE:
                    self._state = _State.LENGTH_WAIT
            else:
                self._header_matched = 0
                # Hatalı bayt yeni bir header başlangıcı olabilir.
                if byte == wireframe.HEADER[0]:
                    self._header_matched = 1

        elif self._state is _State.LENGTH_WAIT:
            if byte < wireframe.MIN_DATA_LEN or byte > self.max_data:
                self._emit_error(Error.INVALID_LENGTH)
                self._reset()
                return
            self._data_len = byte
            self._data_remaining = byte
            self._data = bytearray()
            self._crc = 0
            self._state = _State.DATA_READING

        elif self._state is _State.DATA_READING:
            self._data.append(byte)
            self._crc = wireframe.crc8_update(self._crc, byte)
            self._data_remaining -= 1
            if self._data_remaining == 0:
                self._state = _State.CRC_WAIT

        elif self._state is _State.CRC_WAIT:
            if byte == self._crc:
                if self.on_frame_completed is not None:
                    self.on_frame_completed(bytes(self._data))
            else:
                self._emit_error(Error.CRC_MISMATCH)
            self._reset()

    def _emit_error(self, err: Error) -> None:
        if self.on_error is not None:
            self.on_error(err)

    def _reset(self) -> None:
        self._state = _State.HEADER_WAIT
        self._header_matched = 0
        self._data = bytearray()
        self._data_len = 0
        self._data_remaining = 0
        self._crc = 0
