
249'üncü kanal protokol için ACK mesajlarına ayrılmıştır. Bu kanala 2 düğüm de (hem host hem mcu) hem publisher hem subscriber olur güvenli mesajlaşma için.


Genel Frame Tanımı {
    HEADER
    LENGTH
    CH_ID
    SEQ
    PAYLOAD
    CRC
}

Mesaj Tanımı {
    RESP  : 1 byte = '0x06' (response tipi, ASCII ACK) 
    CH_ID : 1 byte (Kanal ID'si) 
    SEQ   : 1 byte (Mesajın sequence numarası)

    RESP alanı neden var ?
    aslında zaten sadece ACK gönderiliyor NACK yok ama belki ilerleyen zamanlarda eklenir diye bunun için 
}

Frame İçinde Mesajımız {
    HEADER : 4 byte  = fixed 'mros'
    LENGTH : 1 byte  = 
    CH_ID  : 1 byte  = 249 
    SEQ    : 1 byte  = önemli değil işlenmeyecek

    Payload {
        RESP   : 1 byte = 0x06
        CH_ID : 1 byte 
        SEQ   : 1 byte
    }
}


reliable olan kanallarda yayımlayıcılar veriyi gönderdikten sonra, kendi kanal idlerini ve gönderdikleri mesajın sequence numarasını taşıyan bir ack mesajı beklerler. bu ack mesajı gelmeden başka bir mesaj yayınlamazlar. bu duplicate sorununu önler. 

reliable olan kanallarda dinleyiciler veriyi aldıktan sonra kendi kanal idlerini ve aldıkları mesajın sequence numarasını taşıyan bir ack mesajı gönderirler. eğer aynı seq numaralı bir mesaj gelmişse haberleşmede bir sorun olduğunu anlarlar ve tekrar bir ack gönderirler ama veriyi bir daha işlemezler. böylece duplicate sorunu çözülmüş olur.


