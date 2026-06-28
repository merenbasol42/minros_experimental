"""minrospy — minros gömülü mesajlaşma kütüphanesinin Python portu.

minros: bayt tabanlı transportlar (örn. seri port) üzerinde çalışan,
ROS benzeri hafif bir pub/sub mesajlaşma protokolü.

Hızlı başlangıç:
    from minrospy import NodeHL, Transport
    from minrospy.std_msgs import Twist

    node = NodeHL()
    node.transport = Transport(send_bytes=..., read_bytes=..., get_size=..., get_time=...)

    pub = node.create_publisher(Twist, ch_id=1)
    node.create_subscription(Twist, ch_id=1, cb=lambda msg: print(msg.linear.x))

    while True:
        node.spin_once()
"""

from . import core, reliability, std_msgs
from .node import Node, Transport
from .node_hl import NodeHL, Publisher
from .reliability import Reliable

__version__ = "0.1.0"

__all__ = [
    "Node",
    "NodeHL",
    "Transport",
    "Publisher",
    "Reliable",
    "core",
    "reliability",
    "std_msgs",
]
