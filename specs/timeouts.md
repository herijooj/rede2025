# Timeouts

Um *timeout*, em sua definição mais genérica, é um evento que ocorre após um determinado tempo. Se especifica um determinado tempo, o *timeout interval*, e depois desse tempo, alguma coisa acontece. No contexto de redes isso é muito relevante, pois existe a chance de enviarmos algo e não obtermos uma confirmação, porque o outro lado não recebeu a nossa mensagem ou porque o outro lado não conseguiu mandar uma mensagem de resposta para nós. O razoável a se fazer é enviar a nossa mensagem novamente caso nenhuma mensagem seja recebida. No caso geral, é provado matematicamente que é impossível ter certeza que o outro lado recebeu a nossa mensagem, esse é o problema dos dois generais, mas não nos custa ao menos tentar.

Os *sockets* podem ter *timeout* nos seus métodos de `send` e `recv`, como visto no artigo sobre *raw sockets*. Porém, isso não é suficiente para reenviarmos a mensagem só quando um determinado tempo passar, porque os *raw sockets* recebem todos os pacotes da placa de rede. Isso significa que se no meio tempo o seu computador decidir tentar configurar a Internet, o seu *socket* vai receber todas as mensagens dessa transação, mesmo não sendo as que você quer, e isso significa que o *timeout* dessas funções nunca funciona, pois ele sempre será reiniciado com essas outras mensagens da rede. A solução é além de usar o *timeout* no *socket*, é manter o seu próprio *timeout*. Isso pode ser feito simplesmente mantendo o seu próprio relógio.

```c
// usando long long pra (tentar) sobreviver ao ano 2038
long long timestamp() {
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec*1000 + tp.tv_usec/1000;
}

int protocolo_e_valido(char* buffer, int tamanho_buffer) {
    if (tamanho_buffer <= 0) { return 0; }
    // insira a sua validação de protocolo aqui
    return buffer[0] == 0x7f;
}

// retorna -1 se deu timeout, ou quantidade de bytes lidos
int recebe_mensagem(int soquete, int timeoutMillis, char* buffer, int tamanho_buffer) {
    long long comeco = timestamp();
    struct timeval timeout = { .tv_sec = timeoutMillis/1000, .tv_usec = (timeoutMillis%1000) * 1000 };
    setsockopt(soquete, SOL_SOCKET, SO_RCVTIMEO, (char*) &timeout, sizeof(timeout));
    int bytes_lidos;
    do {
        bytes_lidos = recv(soquete, buffer, tamanho_buffer, 0);
        if (protocolo_e_valido(buffer, bytes_lidos)) { return bytes_lidos; }
    } while (timestamp() - comeco <= timeoutMillis);
    return -1;
}
```

Pode ser útil variar o tempo que se espera pela resposta de forma exponencial. Isso significa que na primeira retransmissão você espera um segundo para receber a mensagem, já na próxima espera dois, e na próxima quatro e assim por diante. Isso ajuda no caso por exemplo de um servidor ficar lento e não conseguir responder todas as mensagens que lhe foram enviadas. Assim, as mensagens vão enfileirando, e se elas chegarem num ritmo constante, o servidor nunca vai conseguir responder todas elas. Esse é o conceito do *recuo exponencial*, que é implementado em protocolos como o TCP mas se aplica a muito mais lugares, como por exemplo para evitar colisões na rede através da inserção de um componente probabilístico.

---

* timeouts.txt
* Última modificação em: 2025/04/22 13:19
* por todt

---

Todt
Todt

    Está emraw_socketstarttimeouts

timeouts

Implementando Timeouts

Um timeout, em sua definição mais genérica, é um evento que ocorre após um determinado tempo. Se especifica um determinado tempo, o timeout interval, e depois desse tempo, alguma coisa acontece. No contexto de redes isso é muito relevante, pois existe a chance de enviarmos algo e não obtermos uma confirmação, porque o outro lado não recebeu a nossa mensagem ou porque o outro lado não conseguiu mandar uma mensagem de resposta para nós. O razoável a se fazer é enviar a nossa mensagem novamente caso nenhuma mensagem seja recebida. No caso geral, é provado matematicamente que é impossível ter certeza que o outro lado recebeu a nossa mensagem, esse é o problema dos dois generais, mas não nos custa ao menos tentar.
Sockets

Os sockets podem ter timeout nos seus métodos de send e recv, como visto no artigo sobre raw sockets. Porém, isso não é suficiente para reenviarmos a mensagem só quando um determinado tempo passar, porque os raw sockets recebem todos os pacotes da placa de rede. Isso significa que se no meio tempo o seu computador decidir tentar configurar a Internet, o seu socket vai receber todas as mensagens dessa transação, mesmo não sendo as que você quer, e isso significa que o timeout dessas funções nunca funciona, pois ele sempre será reiniciado com essas outras mensagens da rede. A solução é além de usar o timeout no socket, é manter o seu próprio timeout. Isso pode ser feito simplesmente mantendo o seu próprio relógio.

// usando long long pra (tentar) sobreviver ao ano 2038
long long timestamp() {
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec*1000 + tp.tv_usec/1000;
}
 
int protocolo_e_valido(char* buffer, int tamanho_buffer) {
    if (tamanho_buffer <= 0) { return 0; }
    // insira a sua validação de protocolo aqui
    return buffer[0] == 0x7f;
}
 
// retorna -1 se deu timeout, ou quantidade de bytes lidos
int recebe_mensagem(int soquete, int timeoutMillis, char* buffer, int tamanho_buffer) {
    long long comeco = timestamp();
    struct timeval timeout = { .tv_sec = timeoutMillis/1000, .tv_usec = (timeoutMilis%1000) * 1000 };
    setsockopt(soquete, SOL_SOCKET, SO_RCVTIMEO, (char*) &timeout, sizeof(timeout));
    int bytes_lidos;
    do {
        bytes_lidos = recv(soquete, buffer, tamanho_buffer, 0);
        if (protocolo_e_valido(buffer, bytes_lidos)) { return bytes_lidos; }
    } while (timestamp() - comeco <= timeoutMillis);
    return -1;
}

Recuo Exponencial

Pode ser útil variar o tempo que se espera pela resposta de forma exponencial. Isso significa que na primeira retransmissão você espera um segundo para receber a mensagem, já na próxima espera dois, e na próxima quatro e assim por diante. Isso ajuda no caso por exemplo de um servidor ficar lento e não conseguir responder todas as mensagens que lhe foram enviadas. Assim, as mensagens vão enfileirando, e se elas chegarem num ritmo constante, o servidor nunca vai conseguir responder todas elas. Esse é o conceito do recuo exponencial, que é implementado em protocolos como o TCP mas se aplica a muito mais lugares, como por exemplo para evitar colisões na rede através da inserção de um componente probabilístico.

timeouts.txt

    Última modificação em: 2025/04/22 13:19por todt

Todt
Todt

cc by nc sa

Excepto menção em contrário, o conteúdo neste wiki está sob a seguinte licença:
CC Attribution-Noncommercial-Share Alike 4.0 International

    Bootstrap template for DokuWiki Powered by PHP Valid HTML5 Valid CSS Driven by DokuWiki 

