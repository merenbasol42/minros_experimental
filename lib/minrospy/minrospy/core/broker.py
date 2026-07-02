"""Broker: gelen frame DATA alanını CH_ID bazında callback'lere dağıtır.

Frame DATA layout:
    CH_ID   : data[0]      -> kanal kimliği
    PAYLOAD : data[1:]     -> asıl veri

Not: core SEQ bilmez. Reliability seq'i payload'ın ilk baytında taşır ve kendi
     subscriber wrapper'ında ayıklar — broker bunu opak veri görür.

Kullanım:
    broker = Broker()
    broker.subscribe(ch_id, lambda payload: ...)
    parser.on_frame_completed = broker.on_frame_completed
"""

from collections.abc import Callable

# ChannelCallback: fn(payload) — payload: bytes
ChannelCallback = Callable[[bytes], None]


class Broker:
    def __init__(self):
        # (ch_id, ChannelCallback) listesi
        self._subs: list[tuple[int, ChannelCallback]] = []

    def subscribe(self, ch_id: int, cb: ChannelCallback) -> bool:
        """CH_ID'ye callback kaydeder."""
        if cb is None:
            return False
        self._subs.append((ch_id, cb))
        return True

    def on_frame_completed(self, data: bytes) -> None:
        """Parser'ın on_frame_completed callback'i olarak bağlanır."""
        if len(data) < 1:
            return
        ch_id = data[0]
        payload = data[1:]
        for sub_ch, cb in self._subs:
            if sub_ch == ch_id:
                cb(payload)
