"""minrospy — minros gömülü mesajlaşma kütüphanesinin Python portu.

minros: bayt tabanlı transportlar (örn. seri port) üzerinde çalışan,
ROS benzeri hafif bir pub/sub mesajlaşma protokolü.

Hızlı başlangıç:
    from minrospy import Node, Transport
    from minrospy.std_msgs import Twist

    node = Node()
    node.transport = Transport(send_bytes=..., read_bytes=..., get_size=..., get_time=...)

    pub = node.create_publisher(Twist, ch_id=1)
    node.create_subscription(Twist, ch_id=1, cb=lambda msg: print(msg.linear.x))

    while True:
        node.spin_once()
"""

from . import core, overlays, std_msgs
from .raw_node import RawNode, Transport
from .node import Node, Publisher
from .overlays.reliability import Reliable
from .overlays.logging import Logger, LogSink, Level

__version__ = "0.1.0"

__all__ = [
    "RawNode",
    "Node",
    "Transport",
    "Publisher",
    "Reliable",
    "Logger",
    "LogSink",
    "Level",
    "core",
    "overlays",
    "std_msgs",
]
