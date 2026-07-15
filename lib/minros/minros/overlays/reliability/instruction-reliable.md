# Reliable — tasarım notu

`Reliable`, minros'un güvenilirlik (reliability) katmanıdır. `Sequencer`'ın yerini
alır ve farklı bir konumlandırmaya sahiptir: artık `RawNode`'un içinde değil, `RawNode`'un
**public pub/sub API'sini kullanan bağımsız bir overlay**'dir.

## İlkeler

- **Core'a dokunmaz.** Wire frame'de SEQ alanı yoktur. `Reliable`, seq'i payload'ın
  önüne 1 baytlık opak bir önek olarak koyar: `[SEQ][user bytes]`. Core bunu opak
  veri görür.
- **RawNode'un sıradan bir kullanıcısıdır.** Veriyi `node.publish` ile gönderir, ACK'i
  normal bir kanaldan (CH249) yine `node.publish` ile yollar, abonelikleri
  `node.subscribe` ile yapar. `Node` gerektirmez — ham `RawNode` ile kullanılabilir.
- **Stop-and-wait (window = 1).** Kanal başına aynı anda en fazla 1 uçuştaki frame.
- **Zero-copy / pointer-tutma.** Payload kopyalanmaz; yalnızca `(const u8* + len +
  seq)` tutulur. Timeout olunca `tick()` o pointer'dan **kendisi** yeniden gönderir.
  Eski tasarımdaki "kullanıcıya geri talep eden retransmit callback" **yoktur**.

## Kullanıcı sözleşmesi

- **Publisher:** `publish(ch, buf, len)` çağrıldıktan sonra, ACK gelene
  (`can_send(ch)` tekrar `true` olana) kadar `buf` bozulmamalıdır — `Reliable`
  yalnızca pointer'ını tutar. `can_send`, busy durumunu (önceki ACK'lendi mi)
  bildirir; window=1'de bu sinyal kaçınılmazdır.
- **Subscriber:** seq ayıklama, duplicate filtreleme ve otomatik ACK `Reliable`
  içinde olur; kullanıcı callback'i yalnızca yeni mesajda, seq önekı ayıklanmış
  veriyle çağrılır.

## Node ile

Tipli katmanda buffer'ı kullanıcı düşünmez: reliable `Publisher<MsgT>` mesajı kendi
`buf_[MsgT::SIZE]` üyesine serileştirip onu retransmit backing olarak tutar.
`publish(msg)` busy ise `false` döner (msg bozulmaz).
