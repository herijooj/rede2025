# Raw Sockets

A função **cria_raw_socket** recebe como parâmetro o nome da interface de rede e retorna um descritor de arquivo do *raw socket*. O nome da interface deve ser especificado como uma *string* terminada em nulo (exemplo: "eth0"). Em caso de erro, a função retorna -1.

Segue o código da função:

```c
#include <sys/socket.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <string.h>
#include <stdio.h>

int cria_raw_socket(char *interface) {
    int soquete;
    struct ifreq ir;
    struct sockaddr_ll endereco;
    struct packet_mreq mr;

    // Criação do socket raw
    soquete = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (soquete == -1) {
        perror("Erro ao criar socket");
        return -1;
    }

    // Configuração da interface de rede
    memset(&ir, 0, sizeof(struct ifreq));
    strncpy(ir.ifr_name, interface, IFNAMSIZ);
    if (ioctl(soquete, SIOCGIFINDEX, &ir) == -1) {
        perror("Erro ao configurar interface");
        close(soquete);
        return -1;
    }

    // Configuração do endereço
    memset(&endereco, 0, sizeof(struct sockaddr_ll));
    endereco.sll_family = AF_PACKET;
    endereco.sll_ifindex = ir.ifr_ifindex;
    endereco.sll_protocol = htons(ETH_P_ALL);
    if (bind(soquete, (struct sockaddr *)&endereco, sizeof(struct sockaddr_ll)) == -1) {
        perror("Erro no bind");
        close(soquete);
        return -1;
    }

    // Habilitar modo promíscuo
    memset(&mr, 0, sizeof(struct packet_mreq));
    mr.mr_ifindex = ir.ifr_ifindex;
    mr.mr_type = PACKET_MR_PROMISC;
    if (setsockopt(soquete, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(struct packet_mreq)) == -1) {
        perror("Erro ao habilitar modo promíscuo");
        close(soquete);
        return -1;
    }

    return soquete;
}
```

## Explicação do Código

1. **Criação do Socket**:
   - A função `socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))` cria um socket do tipo *raw* que captura todos os pacotes Ethernet (`ETH_P_ALL`).
   - Em caso de erro, a função retorna -1.

2. **Configuração da Interface**:
   - A estrutura `ifreq` é usada para obter o índice da interface de rede especificada (ex.: "eth0").
   - A chamada `ioctl(soquete, SIOCGIFINDEX, &ir)` associa a interface ao socket.

3. **Vinculação do Socket**:
   - A estrutura `sockaddr_ll` define o endereço do socket, especificando a família (`AF_PACKET`), o índice da interface e o protocolo (`ETH_P_ALL`).
   - A função `bind` vincula o socket à interface de rede.

4. **Modo Promíscuo**:
   - A estrutura `packet_mreq` é configurada para habilitar o modo promíscuo, permitindo que o socket capture todos os pacotes que passam pela interface, mesmo que não sejam destinados ao host.
   - A chamada `setsockopt` aplica essa configuração.

## Exemplo de Uso

```c
int main() {
    int soquete = cria_raw_socket("eth0");
    if (soquete == -1) {
        printf("Falha ao criar o socket\n");
        return 1;
    }
    printf("Socket criado com sucesso: %d\n", soquete);
    close(soquete);
    return 0;
}
```

## Notas

- É necessário ter permissões de superusuário (*root*) para criar e usar *raw sockets* no Linux.
- O modo promíscuo pode gerar um grande volume de dados, dependendo do tráfego na rede.
- Em caso de erro, as mensagens de erro são exibidas usando `perror`, e o socket é fechado antes de retornar -1.

**Fonte**:[](https://wiki.inf.ufpr.br/todt/doku.php?id=raw_socket)

---

Todt
Todt

    Está emraw_socket

raw_socket

Raw Sockets

Uma API para comunicação em rede disponível em sistemas operacionais são os sockets. Eles geralmente abstraem protocolos de rede como o protocolo TCP, criando uma comunicação bidirecional permitindo a comunicação através de funções send e recv.

Nesta disciplina, estamos interessados em criar o nosso próprio protocolo de rede, evitando abstrações ao máximo possível. Para tal, podemos utilizar raw sockets, que nos permitem interagir com a placa de rede quase que diretamente, lendo e escrevendo bytes na rede.

Essa interação mais direta com a placa de rede traz riscos à segurança: qualquer programa pode escrever bytes arbitrários na placa de rede, passando por quaisquer filtros e firewalls presentes no sistema operacional. Além disso, é possível também ler todos os pacotes que estão passando pelo fio desta maneira, o que traz riscos para o usuário que usa aplicações que enviam pacotes descriptografados. Assim, o uso de raw sockets é limitado ao usuário root (ou com capability CAP_NET_RAW) em sistemas Linux. Esse é o motivo, por exemplo, do programa ping rodar como root via setuid, pois utiliza raw sockets para mandar pacotes ICMP.

O resultado é que para rodar aplicações utilizando raw sockets, você vai precisar de um computador com acesso a root. Nos laboratórios do departamento, a maneira mais fácil de fazer isso é utilizando uma distribuição Linux em um pen drive. Qualquer uma pode ser utilizada, sendo uma opção a distribuição antiX Linux, que permite que o sistema operacional inteiro rode em memória, permitindo que o pen drive seja retirado depois da inicialização (o que permite utilizar o mesmo pen drive para fazer setup de dois ou mais computadores!).

Um exemplo de utilização de raw sockets em C segue. A função cria_raw_socket recebe como parâmetro o nome da interface de rede e retorna um descritor de arquivo do raw socket. O nome da interface de rede pode ser descoberto através do comando ip addr que lista todas as interfaces de rede de um sistema Linux. Interfaces de rede de cabo geralmente tem nome da forma enp3s0 ou eth0, e a interface de rede de loopback, que sempre retorna as mensagens enviadas para si mesma geralmente é chamada de lo. As letras “en” significam tipo ethernet, o número aós a letra “p” identifica o barramento PCI e o número após a letra “s” o slot no barramento. As interfaces de rede wireless também podem ser utilizadas, porém o comportamento não é previsível como as de cabo. Interface wireless tem identificador de tipo “wl”.

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <stdlib.h>
#include <stdio.h>
 
int cria_raw_socket(char* nome_interface_rede) {
    // Cria arquivo para o socket sem qualquer protocolo
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
    // Inicializa socket
    if (bind(soquete, (struct sockaddr*) &endereco, sizeof(endereco)) == -1) {
        fprintf(stderr, "Erro ao fazer bind no socket\n");
        exit(-1);
    }
 
    struct packet_mreq mr = {0};
    mr.mr_ifindex = ifindex;
    mr.mr_type = PACKET_MR_PROMISC;
    // Não joga fora o que identifica como lixo: Modo promíscuo
    if (setsockopt(soquete, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) == -1) {
        fprintf(stderr, "Erro ao fazer setsockopt: "
            "Verifique se a interface de rede foi especificada corretamente.\n");
        exit(-1);
    }
 
    return soquete;
}

Neste código, utilizamos também o modo promíscuo, que faz com que protocolos não identificados pelo sistema operacional também sejam lidos pelo socket que criamos. Sem essa opção, o sistema operacional inspeciona cada pacote, procurando apenas protocolos válidos cujo destino é a própria máquina. Como o intuito é definir nosso próprio protocolo que o sistema operacional desconhece, precisamos deste modo para que o nosso código funcione.

Algumas ressalvas importantes sobre os raw sockets:

    Algumas placas de rede possuem algumas regras para envio e recebimento de mensagens. Uma exigência encontrada é por exemplo que o tamanho mínimo de um pacote enviado seja de 14 bytes. Menos que isso, a API send retorna erro. Enviar bytes a mais nesse caso não é problemático, desde que do outro lado, eles sejam ignorados.
    Não é garantido que um pacote seja enviado inteiro, isto é, que ele não seja enviado em vários pedaços, de forma fragmentada. A solução é possibilitar o recebimento de pacotes parciais.
    Placas de rede podem e vão descartar pacotes de rede caso elas identifiquem que podem. Um exemplo clássico é o fato das placas de redes não repassarem pacotes do protocolo VLAN. Geralmente isso é feito como forma de aliviar o trabalho do sistema operacional, deixando certas tarefas a cargo da própria placa de rede (offloading), e o que a sua placa pode fazer geralmente será listado pela ferramenta ethtool. Uma forma de evitar o problema do VLAN é colocar um byte 0xff após bytes que identificam o protocolo VLAN (0x88 e 0x81) e removê-los do outro lado do fio, no receptor.
    O sistema operacional quando detecta uma conexão a cabo, tenta realizar configurações automáticas de rede. Quando se conecta dois computadores e não se configura uma rede, essa configuração vai falhar. Porém, como a configuração é feita através de pacotes enviados na rede, estes pacotes vão aparecer como lixos ocasionais nas chamadas recv.

Timeouts

Assim como sockets normais, raw sockets também podem ter timeouts. Timeouts são maneiras de evitar que uma chamada ao socket, geralmente a recv, bloqueie o sistema por tempo indeterminado. Para tal, é apenas necessário que o socket seja configurado para que depois de um determinado intervalo de tempo, um recv simplesmente falhe. Isso pode ser feito através do setsockopt e a opção SO_RCVTIMEO. Uma estrutura de tempo deve ser passada informando qual o timeout desejado. Lembre-se que o segundo campo da estrutura é em microsegundos, e que os segundos devem ser corretamente especificados no campo de segundos.

const int timeoutMillis = 300; // 300 milisegundos de timeout por exemplo
struct timeval timeout = { .tv_sec = timeoutMillis / 1000, .tv_usec = (timeoutMilis % 1000) * 1000 };
setsockopt(soquete, SOL_SOCKET, SO_RCVTIMEO, (char*) &timeout, sizeof(timeout));

Sempre verifique o retorno do recv, que irá retornar -1 caso dê o socket dê timeout.

raw_socket.txt

    Última modificação em: 2025/04/22 13:14por todt

Todt
Todt

cc by nc sa

Excepto menção em contrário, o conteúdo neste wiki está sob a seguinte licença:
CC Attribution-Noncommercial-Share Alike 4.0 International

    Bootstrap template for DokuWiki Powered by PHP Valid HTML5 Valid CSS Driven by DokuWiki 

