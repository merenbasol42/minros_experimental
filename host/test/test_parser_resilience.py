"""Parser dayanıklılık testleri — gerçek donanım gerekmez.

Soru: Parser, tamamlayamadığı (yarım kalmış / bozuk) bir mesajla
karşılaştığında bir sonraki geçerli frame'i ne kadar çabuk yakalıyor?

`Parser` timeout bilmez — DATA_READING durumundayken akış kesilip yeni bir
frame başlarsa, yeni frame'in HEADER+LEN baytlarını eski (yarım kalmış)
frame'in verisiymiş gibi yutmaya devam eder. Bu da CRC kontrolüne kadar
fark edilmez.

Ama CRC kontrolü (ya da LEN doğrulaması) başarısız olduğunda, parser artık
bütün frame'i çöpe atmıyor: sadece eşleşmiş HEADER'ı (kendi içinde çakışmadığı
için "header değil" olduğu kesin olan tek bölge) atar; LEN+DATA+CRC olarak
tüketilmiş kalan baytları HEADER_WAIT'ten başlayarak yeniden tarar. Böylece
yanlışlıkla DATA'ya gömülmüş gerçek bir frame'in header'ı da yakalanır.
Aşağıdaki testler bunun somut kazanımını (kaç frame kurtarıldığını) ölçüyor
ve state machine'in asla kalıcı olarak kilitlenmediğini (her zaman bir
sonraki header'a resync olduğunu) doğruluyor.

Çalıştırma:
    cd host && python -m pytest test/test_parser_resilience.py -v
"""

import random

from minrospy.core import wireframe
from minrospy.core.parser import Error


# ── Hızlı toparlanma: hata anında state resetlenir ──────────────────────────


def test_recovers_after_random_noise_prefix(parser, collector, framer):
    """Header'a hiç benzemeyen rastgele gürültüden sonra gelen geçerli frame,
    gürültünün uzunluğundan bağımsız olarak doğru parse edilmeli."""
    rng = random.Random(42)
    frame = framer.build(7, b"abc")

    noise = bytes(b for b in (rng.randrange(256) for _ in range(2000)) if b not in wireframe.HEADER)
    noise = noise[:300]

    parser.feed(noise + frame)

    assert collector.frames == [bytes([7]) + b"abc"]
    assert collector.errors == []


def test_recovers_immediately_after_invalid_length(parser, collector, framer):
    """LEN baytı sınır dışıysa parser aynı baytta hata verip resetlenir;
    hemen ardından gelen frame hiç gecikmeden doğru okunmalı."""
    bad = wireframe.HEADER + bytes([0])  # 0 < MIN_DATA_LEN
    good = framer.build(3, b"xy")

    parser.feed(bad + good)

    assert collector.errors == [Error.INVALID_LENGTH]
    assert collector.frames == [bytes([3]) + b"xy"]


def test_recovers_immediately_after_crc_mismatch(parser, collector, framer):
    """CRC uyuşmazlığında da state, CRC baytının hemen ardından resetlenir;
    ekstra bayt beklemeden bir sonraki frame doğru okunur."""
    corrupt = bytearray(framer.build(5, b"data"))
    corrupt[-1] ^= 0xFF  # sadece CRC baytını boz
    good = framer.build(9, b"ok")

    parser.feed(bytes(corrupt) + good)

    assert collector.errors == [Error.CRC_MISMATCH]
    assert collector.frames == [bytes([9]) + b"ok"]


def test_crc_failure_rescans_consumed_bytes_and_recovers_embedded_frame(parser, collector, framer):
    """CRC uyuşmazlığında resync, sadece eşleşmiş 4 baytlık HEADER'ı atar;
    LEN+DATA+CRC olarak tüketilmiş kalan baytlar HEADER_WAIT'ten yeniden
    taranır. Bozuk bir frame'in DATA'sı içine tesadüfen geçerli bir
    header+frame gömülü olsa, artık o iç frame de yakalanır — çöpe gitmez."""
    inner = framer.build(9, b"AB")  # tek başına ele alınsa geçerli olacak bir frame
    outer_data = inner  # ama burada dış frame'in ham DATA'sı olarak gömülü
    wrong_crc = (wireframe.crc8(outer_data) + 1) % 256
    outer = wireframe.HEADER + bytes([len(outer_data)]) + outer_data + bytes([wrong_crc])

    recovery = framer.build(77, b"sonraki")

    parser.feed(outer + recovery)

    # inner, dış (bozuk) frame'in DATA'sı içinde gömülüydü ama resync onu da buldu.
    assert collector.frames == [bytes([9]) + b"AB", bytes([77]) + b"sonraki"]
    assert collector.errors == [Error.CRC_MISMATCH]


def test_overlapping_header_prefix_resyncs(parser, collector, framer):
    """Header'ın kendi baytlarıyla çakışan yarım eşleşme (ör. '..mr' + 'mros')
    state machine'i kilitlememeli; yanlış eşleşen bayt ('m') yeni bir header
    adayı olarak yeniden değerlendirilip doğru senkronizasyon bulunmalı."""
    frame = framer.build(4, b"hey")

    parser.feed(b"mr" + frame)  # "mr" + "mros..." -> içeride "m,r,m,r,o,s" çakışması

    assert collector.frames == [bytes([4]) + b"hey"]
    assert collector.errors == []


# ── Asıl soru: yarım kalmış (tamamlanamayan) bir frame'in maliyeti ─────────


def test_truncated_frame_recovers_via_embedded_rescan(parser, collector, framer):
    """Bir frame LEN ilan edip verinin tamamını göndermeden kesilirse (ör.
    hat kopması), Parser hâlâ DATA_READING durumunda kalır. Hemen ardından
    gelen TAMAMEN GEÇERLİ bir frame'in HEADER+LEN baytları, eski frame'in
    kalan verisi sanılıp yutulur ve declared LEN tamamlanınca CRC_MISMATCH
    üretilir — ama artık bu, o frame'in kaybı anlamına gelmiyor: resync,
    tüketilmiş DATA içinde gömülü kalan frame_a'nın header'ını bulup onu da
    (frame_b ile birlikte) doğru şekilde teslim ediyor."""
    truncated = wireframe.HEADER + bytes([10]) + bytes([0xAA] * 4)  # 10 ilan edildi, 4 geldi, kesildi
    frame_a = framer.build(1, b"AAAA")  # DATA'ya gömülü kalır ama resync ile kurtarılır
    frame_b = framer.build(2, b"BBBB")  # akış zaten bununla toparlanacaktı

    parser.feed(truncated + frame_a + frame_b)

    assert collector.frames == [bytes([1]) + b"AAAA", bytes([2]) + b"BBBB"]
    assert collector.errors == [Error.CRC_MISMATCH]


def test_truncated_frame_never_permanently_locks(parser, collector, framer):
    """Art arda birden çok yarım kalmış frame gelse bile (art arda hat
    kopmaları), Parser er ya da geç mutlaka bir header'a resync olur —
    kalıcı olarak kilitlenmez. CRC_WAIT durumu, eşleşme sonucundan bağımsız
    olarak HER ZAMAN reset ettiği için bu garanti edilir."""
    stream = bytearray()
    for i in range(20):
        # Her biri farklı uzunlukta ilan edip yarıda kesilen 20 bozuk frame.
        declared_len = 5 + (i % 5)
        sent = declared_len - 1  # her zaman en az 1 bayt eksik bırak
        stream += wireframe.HEADER + bytes([declared_len]) + bytes([0xBB] * sent)

    good = framer.build(42, b"toparlandi")
    stream += good

    parser.feed(bytes(stream))

    assert collector.frames == [bytes([42]) + b"toparlandi"]


def test_large_declared_length_can_silently_swallow_several_frames(parser, collector, framer):
    """DİKKAT — bu, kütüphanenin en kırılgan noktası: yarım kalan frame'in
    ilan ettiği LEN büyükse (MAX_DATA_LEN'e yakın), takip eden BİRDEN FAZLA
    geçerli frame hiçbir hata bile üretilmeden sessizce yutulabilir. CRC
    kontrolü, ilan edilen bütün veri toplanana kadar devreye girmiyor; yani
    "kaç bayt kayboldu" sinyali declared_len tamamlanana dek hiç gelmez.
    Toparlanma maliyeti "bir frame" ile sınırlı değil — üst sınırı sadece
    protokolün LEN alanı (max_data, en fazla 249 bayt) belirliyor."""
    declared_len = wireframe.MAX_DATA_LEN
    truncated = wireframe.HEADER + bytes([declared_len])  # veri hiç gelmeden kesildi

    swallowed = [framer.build(i, b"x" * 4) for i in range(5)]  # bunların hepsi kaybolacak
    swallowed_bytes = b"".join(swallowed)

    parser.feed(truncated + swallowed_bytes)

    # declared_len (249) henüz tamamlanmadı -> CRC kontrolüne hiç ulaşılmadı,
    # dolayısıyla ne bir frame ne de bir hata sinyali var. Sessiz veri kaybı.
    assert collector.frames == []
    assert collector.errors == []


def test_recovery_boundary_no_longer_loses_any_frame(parser, collector, framer):
    """declared_len tam tamamlandığı an CRC_WAIT'e geçilir ve HEMEN sıradaki
    bayt CRC adayı olarak tüketilir — bu bayt genelde bir sonraki geçerli
    frame'in HEADER'ının ilk baytıdır, bu yüzden CRC_MISMATCH kaçınılmaz.
    Ama artık bu hata bir kayıp anlamına gelmiyor: declared_len boyunca
    tüketilen 249 baytlık DATA içine gömülü TÜM frame'ler (5 "swallowed"
    frame, "finally-ok" ve sınırdaki "still-lost") resync tarafından tek tek
    bulunup teslim ediliyor — üstüne hiçbir ekstra bayt beklemeden."""
    declared_len = wireframe.MAX_DATA_LEN
    truncated = wireframe.HEADER + bytes([declared_len])

    swallowed = [framer.build(i, b"x" * 4) for i in range(5)]
    swallowed_bytes = b"".join(swallowed)
    also_lost = framer.build(99, b"finally-ok")  # artık kaybolmuyor: declared_len sınırına denk gelse de resync yakalıyor
    pad_len = declared_len - len(swallowed_bytes) - len(also_lost)
    pad = bytes([0xEE] * pad_len)  # declared_len'i tam tamamlamak için dolgu

    frame_after_boundary = framer.build(55, b"still-lost")  # artık kaybolmuyor: resync ile kurtarılıyor
    frame_recovered = framer.build(66, b"recovered!")

    parser.feed(truncated + swallowed_bytes + also_lost + pad + frame_after_boundary + frame_recovered)

    assert collector.frames == [
        bytes([0]) + b"xxxx",
        bytes([1]) + b"xxxx",
        bytes([2]) + b"xxxx",
        bytes([3]) + b"xxxx",
        bytes([4]) + b"xxxx",
        bytes([99]) + b"finally-ok",
        bytes([55]) + b"still-lost",
        bytes([66]) + b"recovered!",
    ]
    assert collector.errors == [Error.CRC_MISMATCH]


# ── Beslenme granülerliği state machine'i etkilememeli ─────────────────────


def test_byte_by_byte_feed_matches_bulk_feed(framer):
    """feed() ister tek seferde ister bayt bayt çağrılsın (gerçek seri porttan
    okurken olduğu gibi), sonuç aynı olmalı."""
    from minrospy.core.parser import Parser

    stream = (
        b"garbage-before"
        + framer.build(1, b"one")
        + framer.build(2, b"two-two")
        + wireframe.HEADER + bytes([255])  # geçersiz LEN (> MAX_DATA_LEN)
        + framer.build(3, b"three")
    )

    bulk_frames, bulk_errors = [], []
    p_bulk = Parser()
    p_bulk.on_frame_completed = bulk_frames.append
    p_bulk.on_error = bulk_errors.append
    p_bulk.feed(stream)

    byte_frames, byte_errors = [], []
    p_byte = Parser()
    p_byte.on_frame_completed = byte_frames.append
    p_byte.on_error = byte_errors.append
    for b in stream:
        p_byte.feed(bytes([b]))

    assert bulk_frames == byte_frames == [bytes([1]) + b"one", bytes([2]) + b"two-two", bytes([3]) + b"three"]
    assert bulk_errors == byte_errors == [Error.INVALID_LENGTH]
