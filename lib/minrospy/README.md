# minrospy

[minros](../lib/minros) gömülü mesajlaşma kütüphanesinin Python portu. Bayt tabanlı
transportlar (seri port, TCP, loopback…) üzerinde çalışan, ROS benzeri hafif bir
pub/sub protokolü sağlar. Wire formatı C++ minros ile **birebir uyumludur** —
bir MCU'da çalışan minros düğümü ile bir PC'de çalışan minrospy düğümü doğrudan
haberleşebilir.

## Mimari

C++ kütüphanesinin katmanları korunmuştur. Core (wireframe/framer/parser/broker)
ve `RawNode` güvenilirlikten **habersizdir**; seq diye bir wire alanı yoktur.
`Reliable`, RawNode'un public pub/sub API'sini kullanan bağımsız bir **overlay**'dir:
seq'i payload'ın önüne opak bir önek olarak koyar, ACK'i normal bir kanaldan
(CH249) yollar.

| Katman | Modül | Sorumluluk |
|--------|-------|------------|
| Wire formatı | [`core/wireframe.py`](minrospy/core/wireframe.py) | Frame sabitleri + CRC-8/SMBUS |
| Framer | [`core/framer.py`](minrospy/core/framer.py) | Payload → wire frame (opak head önekli) |
| Parser | [`core/parser.py`](minrospy/core/parser.py) | Byte akışı → frame (durum makinesi) |
| Broker | [`core/broker.py`](minrospy/core/broker.py) | CH_ID bazında dağıtım |
| RawNode | [`raw_node.py`](minrospy/raw_node.py) | Saf ham byte API (reliability'den habersiz) |
| Reliable | [`reliability/reliable.py`](minrospy/reliability/reliable.py) | seq / ACK / retransmit / dedup — RawNode üzerine overlay |
| Node | [`node.py`](minrospy/node.py) | RawNode + Reliable üzerine tipli yüksek seviye sarmalayıcı |
| std_msgs | [`std_msgs/`](minrospy/std_msgs) | Float32, Vector3, Twist, … |

## Frame formatı

```
[HEADER 'mros'(4)] [LEN(1)] [CH_ID(1)] [PAYLOAD(1..248)] [CRC-8(1)]
```

- `LEN` = CH_ID + PAYLOAD uzunluğu (2..249)
- `CRC` = CRC-8/SMBUS (poly 0x07, init 0x00) — DATA byte'ları üzerinden
- Çok baytlı alanlar **little-endian**
- Core SEQ bilmez. Güvenilir mesajlarda `Reliable`, PAYLOAD'ın önüne 1 baytlık
  seq öneki koyar (`[SEQ][user bytes]`); ACK frame'leri CH249'da `[RESP][CH][SEQ]`
  taşır. Bu, core için opak veridir.

## Transport

`Transport` dört callable tutar:

```python
Transport(
    send_bytes = lambda data: ...,   # bytes yaz
    read_bytes = lambda n: ...,      # en fazla n bytes oku -> bytes
    get_size   = lambda: ...,        # okunmaya hazır byte sayısı -> int
    get_time   = lambda: ...,        # ms cinsinden zaman -> int
)
```

## Kullanım — tipli API

```python
from minrospy import Node, Transport
from minrospy.std_msgs import Twist, Vector3

node = Node()
node.transport = Transport(send_bytes=..., read_bytes=..., get_size=..., get_time=...)

# Publisher
pub = node.create_publisher(Twist, ch_id=1)
pub.publish(Twist(Vector3(0.5, 0, 0), Vector3(0, 0, 0.2)))

# Subscriber — callback doğrudan tipli mesaj alır
node.create_subscription(Twist, ch_id=1, cb=lambda msg: print(msg.linear.x))

# Güvenilir publisher (ACK + retransmit) — retransmit otonomdur, callback gerekmez
pub = node.create_publisher(Twist, ch_id=2, reliable=True)
if not pub.publish(Twist(...)):
    ...  # önceki mesaj hâlâ uçuşta (ACK bekleniyor), sonra dene

# Güvenilir subscriber
node.create_subscription(Twist, ch_id=2, cb=lambda msg: ..., reliable=True)

while True:
    node.spin_once()   # parser + reliable tick birlikte
```

## Kullanım — düşük seviye API

`RawNode` saf ham byte transporttur; güvenilirlik isteyen `Reliable` overlay'ini takar.

```python
from minrospy import RawNode, Reliable, Transport

node = RawNode()
node.transport = Transport(...)

# Unreliable — callback ham payload alır
node.subscribe(5, lambda payload: print(payload))
node.publish(5, b"\xde\xad\xbe\xef")

# Reliable — overlay'i node'a tak (dedup + otomatik ACK + otonom retransmit)
rel = Reliable(node)
rel.subscribe(6, lambda payload: print("reliable:", payload))

while True:
    node.spin_once()   # gelen baytlar
    rel.tick()         # timeout/retransmit
    if rel.can_send(6):
        rel.publish(6, b"\x01\x02")
```

## Pyserial ile gerçek transport örneği

```python
import time, serial
from minrospy import Node, Transport
from minrospy.std_msgs import Float32

ser = serial.Serial("/dev/ttyUSB0", 115200, timeout=0)
node = Node()
node.transport = Transport(
    send_bytes=ser.write,
    read_bytes=ser.read,
    get_size=lambda: ser.in_waiting,
    get_time=lambda: int(time.monotonic() * 1000),
)

node.create_subscription(Float32, 1, lambda m: print("sıcaklık:", m.value))
while True:
    node.spin_once()
```

## Testler

```bash
python minrospy/tests/test_minrospy.py
# veya
python -m pytest minrospy/tests
```

Wire uyumluluğu, C++ minros ile paylaşılan tarafsız altın vektörlere karşı
[`conformance/`](../../conformance) altında doğrulanır; iki implementasyon
birbirinden kayarsa testlerden biri kırmızıya döner:

```bash
./conformance/run.sh
```
