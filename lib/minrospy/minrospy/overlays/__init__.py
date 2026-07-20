"""minrospy.overlays — core üzerine oturan bağımsız protokol katmanları.

Her overlay yalnızca RawNode'un public publish/subscribe API'sini kullanır;
core'a hiçbir şey eklemez. C++ minros::overlays ile simetriktir.

    reliability : ACK + retransmit (stop-and-wait) güvenilirlik
    logging     : best-effort log yayını + parça birleştirme (sink)
    parameters  : çalışma-zamanı parametre get/set (REQ/RES kanalları)
"""

from . import logging, parameters, reliability

__all__ = ["logging", "parameters", "reliability"]
