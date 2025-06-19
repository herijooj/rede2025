# UNIVERSIDADE FEDERAL DO PARANÁ

## BACHARELADO EM CIÊNCIA DA COMPUTAÇÃO

### CI1058 REDES 1

**Semestre 2025/1**
**Professores Luiz Albini e Eduardo Todt**
**Versão: 20250424**

---

## TRABALHO T1

### 1. Descrição

O objetivo deste trabalho é implementar um jogo, **caça ao tesouro**, descrito a seguir. No contexto de Redes I, a ideia é trabalhar com protocolos de nível 2 na comunicação entre computadores por mensagens curtas e transferência de arquivos. Deve ser implementado um modelo **cliente-servidor**, com comunicação baseada em *raw sockets*. O cliente e o servidor podem ser o mesmo programa, com parâmetros de chamada especificando a funcionalidade, ou dois programas especializados em cada papel.

O servidor e o cliente devem executar em máquinas distintas. As máquinas devem operar em modo *root* (pode fazer boot por pendrive). As máquinas devem estar interligadas por cabo de rede diretamente, sem utilizar a rede do departamento (os frames definidos são rejeitados pelos switches).

O servidor controla o mapa e os tesouros. Na tela do servidor são mostrados a posição dos tesouros, a lista dos movimentos realizados pelo usuário e a posição atual do usuário. O usuário somente interage no cliente.

Uma vez conectados e iniciados cliente e servidor, no cliente é mostrado um mapa, que consiste de um grid `8x8`. O canto esquerdo inferior tem coordenadas `(0,0)`. O servidor sorteia 8 posições no grid, que correspondem às localizações de tesouros. A localização dos tesouros não é transferida ao cliente. As posições são indicadas no console do servidor, bem como log de movimentos e encontros de objetos. O sorteio é realizado na inicialização do servidor.

Os tesouros são lidos de arquivos no servidor, quando o jogador atinge pela primeira vez a posição no grid que contém cada objeto. Uma questão que complica um pouco: os tesouros podem ser arquivos de imagens (`.jpg`), vídeos (`.mp4`) ou texto (`.txt`). Os arquivos devem estar em um diretório chamado `objetos`, com nomes sendo números de `1` a `8`, com as terminações adequadas. Os textos poderão ser grandes, extraídos do site do Projeto Gutenberg. Para facilitar a missão de vocês, o nome de cada arquivo pode ter no máximo `63` bytes, possuindo somente caracteres ASCII na faixa `0x20–0x7E`. É sabido que haverá arquivos nomeados `1.???` a `8.???`, sendo `???` variável conforme o tipo de arquivo. É necessário testar isso para saber que ação tomar para o envio e exibição.

Ao serem trazidos ao cliente, tesouros devem ser exibidos conforme o tipo que forem.
Marcar no grid as posições percorridas e de forma diferente as posições que contêm tesouros.

O jogador indica no cliente em que direção deve mover uma posição o agente jogador no grid. Movimentos para fora do grid devem ser ignorados, opcionalmente com sinalização do erro.

A interface é livre, podem fazer desde muito simples em console até algo sofisticado.

Deve ser seguido o protocolo de comunicação criado em aula. A implementação pode ser em C, C++ ou Go.

---

### Protocolo

O protocolo é inspirado no Kermit e possui os seguintes campos, na ordem que eles são enviados na rede:

| marcador início                                             | tamanho | sequência | tipo   | checksum | … dados …     |
| ----------------------------------------------------------- | ------- | --------- | ------ | -------- | ------------- |
| 0111 1110 (8 bits)                                          | 7 bits  | 5 bits    | 4 bits | 8 bits   | 0 a 127 bytes |
| *(Bytes sobre campos de dados: tamanho, seq, tipo e dados)* |         |           |        |          |               |

#### Tipos de mensagem:

* `0`: ack
* `1`: nack
* `2`: ok + ack
* `3`: livre
* `4`: tamanho
* `5`: dados
* `6`: texto + ack + nome
* `7`: video + ack + nome
* `8`: imagem + ack + nome
* `9`: fim arquivo
* `10`: desloca para direita
* `11`: desloca para cima
* `12`: desloca para baixo
* `13`: desloca para esquerda
* `14`: livre
* `15`: erro

#### Códigos de erro:

* `0`: sem permissão de acesso
* `1`: espaço insuficiente

---

### Informações adicionais

* Campos do frame, códigos de erro e outras variáveis de controle devem ser `unsigned`. Use `uchar` sempre que possível.
* Não utilize `float` em campos de protocolo.
* O protocolo deve utilizar controle de fluxo por **stop-and-wait**. A implementação com **janelas deslizantes de tamanho 3, go-back-N** (volta N) gera pontos extra, desde que o resultado final não ultrapasse 100.
* É necessário tratar timeouts e possíveis erros e falhas na comunicação.
* Para verificar se uma sequência vem depois da outra, considere a diferença das sequências: se a diferença for grande o suficiente para estarem no meio, ou nas bordas? Isso dá a pista para a aritmética de números seriais.
* Para verificar quanto espaço livre uma máquina tem (e ter uma certa tolerância), use a função `statvfs`.

  * Espaço livre em bytes: `st.f_bsize * st.f_bavail`, onde `st` é a estrutura `statvfs`.
* Para descobrir que o arquivo é regular e o tamanho do arquivo, utilize a função `stat`.

  * Tamanho do arquivo em bytes: `st.st_size`, onde `st` é a estrutura `stat`.
* Note que há troca de responsabilidade de enviar `ACKs` e `NACKs`. Apenas uma das partes (servidor ou cliente) é responsável pelo envio, mas pode acontecer que elas não saibam que precisam trocar de responsabilidade. Recomenda-se armazenar a última mensagem recebida e a última mensagem enviada para reenvio, caso necessário.

---

### Entrega

É obrigatório ter relatório (formato artigo SBC) impresso no momento da apresentação, bem como ter submetido os entregáveis no UFPR Virtual.
A apresentação será realizada em laboratório do DINF, com agendamento a ser definido. A apresentação tem grande influência na nota — todos os membros do grupo devem estar presentes e dominar totalmente o trabalho realizado. Os professores fornecerão arquivos de teste.
Não pode haver atraso na entrega.

---

### Registro da definição em aula

Seguem fotos do quadro tomadas durante a aula em que construímos a especificação.

---

### Controle de versões

* `20250415`: versão inicial
* `20250424`: corrigido controle de fluxo para stop-and-wait com janela 3 opcional, esclarecimento sobre nomes de arquivos