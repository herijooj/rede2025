Aqui está o conteúdo da página **Timeouts** convertido para Markdown compatível com Obsidian:

````markdown
### Implementando Timeouts

Um *timeout*, em sua definição mais genérica, é um evento que ocorre após um determinado tempo. Se especifica um determinado tempo (timeout interval), e depois desse tempo algo acontece. No contexto de redes, isso é importante porque podemos enviar uma mensagem e não obter confirmação—o que justifica retransmissões. Matematicamente, é impossível garantir que a outra parte recebeu a mensagem (problema dos dois generais), mas é razoável tentar :contentReference[oaicite:1]{index=1}.

---

### Sockets

- Sockets podem ter timeout nos métodos `send` e `recv`, como visto no artigo de raw sockets.
- Mas isso não é suficiente: raw sockets recebem **todos** os pacotes da interface, mesmo os irrelevantes, reiniciando o timeout.
- A saída: além de usar SO_RCVTIMEO, manter nosso próprio timeout com um relógio interno :contentReference[oaicite:2]{index=2}.

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
    struct timeval timeout = {
        .tv_sec = timeoutMillis/1000,
        .tv_usec = (timeoutMillis%1000) * 1000
    };
    setsockopt(soquete, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    int bytes_lidos;
    do {
        bytes_lidos = recv(soquete, buffer, tamanho_buffer, 0);
        if (protocolo_e_valido(buffer, bytes_lidos)) { 
            return bytes_lidos; 
        }
    } while (timestamp() - comeco <= timeoutMillis);
    return -1;
}
````

---

### Recuo Exponencial

* É útil aumentar o tempo de espera de forma exponencial:

  * 1ª retransmissão: espera 1 s
  * 2ª retransmissão: espera 2 s
  * 3ª retransmissão: espera 4 s
* Método comum em TCP, também usado para evitar colisões na rede ([wiki.inf.ufpr.br][1]).

---

*Fonte:* Adaptado de “timeouts.txt”, última modificação em 22/04/2025&#x20;
Licença: CC Attribution‑Noncommercial‑Share Alike 4.0 International