
248'inci kanal protokol için LOG mesajlarına ayrılmıştır. Pratikte slave yayınlar, host (minrospy) sink olur; nokta-nokta link, tek yönlü akış.

Rezerve protokol kanal bloğu:
    249 = reliability ACK
    248 = logging
Bu blok overlay'lere ayrılmıştır; kullanıcı kanalları buraya çakışmamalıdır.


Genel Frame Tanımı {
    HEADER
    LENGTH
    CH_ID
    PAYLOAD          <- core FLAGS/level bilmez; opak head önekidir
    CRC
}

Log veri frame'i {
    logging katmanı, kullanıcı text'inin önüne 1 baytlık FLAGS öneki koyar.
    PAYLOAD = [FLAGS (1 byte)][text/bytes...]
    Bu önek core için opaktır; yalnızca LogSink wrapper'ı ayıklar.
}

FLAGS bit yerleşimi (1 byte) {
    bit 0       LAST   : 1 → log'un son (veya tek) parçası
    bit 1..3    LEVEL  : seviye 0..4 (DEBUG,INFO,WARN,ERROR,FATAL); her parçada taşınır
    bit 4..7    SEQ4   : 0..15 dönen parça sayacı (kayıp/atlama tespiti)
}

Seviyeler {
    0 = DEBUG
    1 = INFO
    2 = WARN
    3 = ERROR
    4 = FATAL
}


Neden \0 sentinel değil de açık FLAGS başlığı ?
    Frame'ler zaten LENGTH ile sınırlı; bir log'un bittiği bilinir. \0 yalnızca
    "birden fazla frame'e taşıyor" sinyali için gerekirdi ama sentinel-tarama:
      • binary-safe değildir (text içindeki 0x00 bölünmeyi bozar),
      • unreliable kanalda düşen parçayı tespit edemez → iki log'un parçalarını
        sessizce birleştirip bozuk satır üretebilir.
    Açık FLAGS başlığı: LAST biti bitişi, SEQ4 süreklilik/kayıp tespitini verir.


Parçalama (fragmentation) {
    FRAME_DATA küçük kurulabildiği (ör. 32) için uzun log satırı tek frame'e
    sığmayabilir. logging::Logger metni CHUNK = FRAME_DATA - 2 (CH_ID + FLAGS)
    boyutlu parçalara böler:
      • ilk parça SEQ4=0 ile başlar, her parça +1 taşır (16'da sarar),
      • son parça LAST=1 taşır,
      • tek frame'e sığan log = LAST=1, SEQ4=0.

    Publisher tarafında birleştirme buffer'ı YOKTUR — sadece flash string'e bir
    index tutulur, parçalar framer'ın kendi TX buffer'ından yayılır (zero-copy).
}

Sink (host) yeniden birleştirme {
    LogSink parçaları REASM_BUF içinde toplar:
      • yeni mesaj SEQ4=0 ile başlar; ortadan (seq!=0) yakalanırsa parça atılır,
      • beklenen SEQ4 gelmezse yarım satır atılır (dropped++),
      • buffer taşarsa satır atılır (dropped++),
      • LAST görülünce tam satır cb(level, msg, len) ile teslim edilir.
    Reassembly buffer'ı yalnızca sink'te vardır; publisher (slave) RAM ödemez.
}

Best-effort (unreliable) {
    Log kanalı reliability overlay'ine sokulmaz: ACK/retransmit yoktur. Log
    kaybolabilir — bu kabul edilebilir davranıştır; loglama asla ana akışı
    bloklamaz. Kaynakta min_level filtresi ile eşik altı çağrılar wire'a hiç
    dokunmaz.
}


AVR NOTU {
    Bu tasarım flash'ı memory-mapped olan hedefleri varsayar (Cortex-M/STM32,
    ESP32). Klasik AVR (Harvard) mimarisinde flash memory-mapped değildir;
    string literal'ler ya RAM'e kopyalanır ya da PROGMEM + pgm_read_byte
    gerektirir. Framer'ın düz payload[i] okuması bunu yapamayacağından, flash'tan
    zero-copy log (ve parçalama) AVR'de OLDUĞU GİBİ ÇALIŞMAZ. AVR desteği
    gerekiyorsa PROGMEM-aware ayrı bir okuma yolu tasarlanmalıdır.
}
