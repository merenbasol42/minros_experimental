

sequencer, minros için reliability protocolü yönetecek olan classtır.

sequencer güvenilir publisher, subscriber oluşturmalı. sequence ve ack işlemlerini kullanıcı yerine halletmeli.  

bunu yaparken de yeniden gönderme durumunda kullanıcıdan yeniden talep etmeli bir callback ile 
