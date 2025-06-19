Aqui está o conteúdo da página **Raw Sockets** da wiki formatado em Markdown, compatível com o Obsidian:

````markdown
# Raw Sockets

Uma API para comunicação em rede disponível em sistemas operacionais são os sockets. Eles geralmente abstraem protocolos como TCP, criando comunicação bidirecional via `send` e `recv`.

Nesta disciplina, queremos implementar nosso próprio protocolo de rede, evitando abstrações. Para isso, usamos *raw sockets*: interfaces quase diretas com a placa de rede, permitindo leitura e escrita de bytes na rede. Porém, isso traz riscos à segurança: qualquer programa pode enviar bytes arbitrários, ultrapassando filtros OS, e ler todos os pacotes trafegando — por isso, o uso de raw sockets é restrito ao usuário root (ou capability `CAP_NET_RAW`) em Linux. É por isso que o `ping` roda como root via setuid :contentReference[oaicite:1]{index=1}.

Para usar raw sockets, você precisa de acesso root. Nos laboratórios, uma forma prática é rodar Linux via pen drive (ex: antiX Linux), carregando todo o SO na memória e depois removendo o pendrive :contentReference[oaicite:2]{index=2}.

---

## Exemplo em C

```c
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <stdlib.h>
#include <stdio.h>

int cria_raw_socket(char* nome_interface_rede) {
    int soquete = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (soquete == -1) {
        fprintf(stderr, "Erro ao criar socket: Verifique se você é root!\n");
        exit(-1);
    }

    int ifindex = if_nametoindex(nome_interface_rede);

    struct sockaddr_ll endereco = {0};
    endereco.sll_family = AF_PACKET;
    endereco.sll_protocol = htons(ETH_P_ALL);
    endereco.sll_ifindex = ifindex;
    if (bind(soquete, (struct sockaddr*) &endereco, sizeof(endereco)) == -1) {
        fprintf(stderr, "Erro ao fazer bind no socket\n");
        exit(-1);
    }

    struct packet_mreq mr = {0};
    mr.mr_ifindex = ifindex;
    mr.mr_type = PACKET_MR_PROMISC;
    if (setsockopt(soquete, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) == -1) {
        fprintf(stderr, "Erro ao fazer setsockopt: Verifique se a interface está correta.\n");
        exit(-1);
    }

    return soquete;
}
````

Este código ativa o modo **promíscuo**, permitindo receber também pacotes de protocolos não reconhecidos pelo sistema. Sem isso, o kernel filtraria os pacotes e não passaríamos nosso próprio protocolo ([wiki.inf.ufpr.br][1]).

---

## Observações importantes

* Algumas placas exigem tamanho mínimo de pacote (ex: 14 bytes). `send` pode falhar caso seja menor. Enviar bytes extras é aceitável se o receptor souber ignorá-los ([wiki.inf.ufpr.br][1]).
* Pacotes podem ser fragmentados. Seu código deve suportar recepção parcial .
* Placas podem descarregar pacotes (ex: VLAN) para aliviar a carga (offloading). Use `ethtool` para verificar capacidades. Uma solução é inserir um byte `0xff` após o identificador VLAN (`0x88`, `0x81`) e ignorá-lo no receptor ([wiki.inf.ufpr.br][1]).
* Se o OS tenta autoconnect via cabo, pacotes “lixo” podem aparecer em `recv` ([wiki.inf.ufpr.br][1]).

---

## Timeouts

Raw sockets suportam timeouts via `setsockopt(..., SO_RCVTIMEO, ...)`:

```c
const int timeoutMillis = 300; // 300 ms
struct timeval timeout = {
  .tv_sec = timeoutMillis / 1000,
  .tv_usec = (timeoutMillis % 1000) * 1000
};
setsockopt(soquete, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
```

No `recv`, se der timeout, retorna `-1`. ([wiki.inf.ufpr.br][1])

---

## Licença

Conteúdo licenciado sob *CC Attribution-Noncommercial-Share Alike 4.0* ([wiki.inf.ufpr.br][1]).