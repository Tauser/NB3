# Vida própria — humor, estados, personalidade e memória local

Especificação da camada que devolve ao NB3 a autonomia do Noise: o xiaozhi
continua dono da conversa rápida, e o ESP32 mantém humor contínuo,
temperamento, energia, estados comportamentais, reflexos e iniciativa. O
microSD guarda memória de médio e longo prazo, acessível pelo próprio
firmware e pela conversa via ferramentas MCP. Decisão: D-010.

Regra de divisão: **o SD armazena; o ESP32 executa a lógica.** Nenhum serviço
no PC é necessário para o robô funcionar.

## 1. Quem é dono do quê

| Camada | Dono | Observação |
|---|---|---|
| Sessão de conversa, áudio, ASR/LLM/TTS | xiaozhi + xiaozhi.me | intocado (tag + patch) |
| Estados operacionais | xiaozhi (`DeviceState`) | starting, wifi_configuring, idle, connecting, listening, speaking, notifying, upgrading, activating, audio_testing, fatal_error |
| Estados comportamentais | NB3 | camada acima, consome os operacionais como eventos |
| Humor, energia, personalidade | NB3 | núcleos C17 com host-test |
| Memória média e longa | NB3, persistida no microSD | lógica no ESP32, SD só armazena |
| Expressão, LEDs (e depois motion) | árbitro do NB3 | único escritor |

A camada comportamental **não duplica a máquina de conversa**: ela não decide
quando ouvir ou falar. Recebe as mudanças de `DeviceState` (hoje via
`Led::OnStateChanged()`) e as traduz em eventos próprios.

## 2. Estado, humor e energia são eixos separados

- **Estado comportamental** — o que o robô está fazendo: `IDLE`, `ENGAGED`
  (há conversa: listening/speaking/notifying), `REACTING` (toque, reflexo ou
  arco em curso), `SLEEPING`, `FAULT` (fatal_error do xiaozhi ou do
  `motion_safety`). Modo `quiet` é flag persistente, não estado.
- **Humor** — como ele se sente: vetor contínuo valência × ativação em
  [−1, +1]. Pode estar em IDLE e continuar contente ou irritado.
- **Energia** — disposição: cansaço que segue o relógio do dia e tédio que
  cresce no repouso sem companhia (§3.3).

**Voltar ao IDLE limpa ações temporárias** (propostas transitórias do
árbitro, reação de toque, olhar e, no futuro, pescoço) e **preserva a
evolução do humor e da energia**. O humor só muda por estímulo e decaimento,
nunca por troca de estado.

## 3. Humor

Lógica numérica do `emotion_core` do NB2, sem o renderer antigo:

- estímulos somam deltas `{Δv, Δa}` com clamp por eixo;
- decaimento exponencial rumo ao **temperamento habitual** (no NB2,
  `(+0,10, +0,05)`: em paz, levemente caloroso e desperto), não ao zero;
- **recuperação assimétrica**: enquanto a valência é negativa, ela decai
  mais devagar (no NB2, constante 2× maior) — mágoa passa devagar;
- ativação recebe um offset circadiano quando a fonte de hora existir.

A recuperação padrão do NB3 é **120 s** até 5% (o NB2 usava 60 s). O NB2
misturava faces continuamente e mostrava desvios pequenos; aqui o humor só
aparece acima de um limiar, e com 60 s ele ficava visível uns 10 s. Com 120 s
e limiares de 0,25 (aparece) e 0,18 (some), algumas respostas alegres ou um
carinho de ~15 s deixam a expressão uns 30 s depois do que a causou.

**O humor atual vive em RAM e reinicia no boot**, no temperamento. Ele não é
restaurado do SD: uma irritação antiga nunca volta como se nenhum tempo
tivesse passado. O que atravessa o reinício são os episódios (§5), que
influenciam o novo humor pelo viés descrito abaixo, já com a idade real.

### 3.1 Estímulos

Fontes: emoção recebida do xiaozhi (`SetEmotion`), toque, eventos de
conversa (wake, fala longa) e a ferramenta MCP de estímulo (§6). O corpo
**integra, não obedece**: todo delta passa pela habituação.

**Habituação** — cada estímulo tem uma chave (`origem:tag`, ex.
`xiaozhi:happy`, `touch:tap`, `mcp:elogio`). Dentro de uma janela por chave
(ordem de 30 s), a n-ésima repetição aplica `delta × hⁿ`, e a soma dos
módulos aplicados por chave na janela tem teto. O fator `h` é 0,5 por
padrão: receber "happy" a cada frase da resposta empurra o humor uma vez com
força e depois quase nada. Prazer contínuo habitua mais devagar (carinho:
`h` ≈ 0,8), senão os pulsos somem enquanto o carinho ainda dura. Estados
repetidos do xiaozhi só contam na troca de estado. A ferramenta MCP tem teto
próprio, menor que o do toque.

**Memória não é estímulo.** Episódios não somam deltas; eles deslocam o
**alvo do decaimento** (o temperamento do momento) por um viés que é função
pura dos episódios e da hora atual (importância × valência), limitado a uma
faixa pequena (ordem de ±0,15) e recalculado num ciclo lento (ordem de 1 min)
e quando a hora fica válida. Por ser recalculado e não integrado, não
acumula: consultar uma lembrança pelo MCP, mil vezes, não muda o humor.

### 3.2 Saída e prioridade

O vetor escolhe uma expressão do vocabulário de 14 (com histerese para não
alternar na fronteira) e a propõe ao árbitro como fonte `MOOD`:

| Prioridade | Fonte | Transitória |
|---|---|---|
| 60 | falha (`kFault`) | não |
| 50 | reflexo | sim |
| 40 | toque | sim |
| 30 | comando MCP | não; expira no prazo pedido |
| 20 | emoção da conversa | sim |
| 5 | estado operacional (escuta, conexão, Wi-Fi) | sim |
| **3** | **humor (`MOOD`)** | **não** |
| — | baseline do árbitro | — |

O comando MCP de expressão é explícito e tem prazo, então sobrevive à volta
ao IDLE: "faz cara de bravo por 30 segundos" dura 30 s. Toque e falha o
encobrem; ao terminarem, ele reaparece se ainda estiver no prazo, que continua
correndo por baixo.

Os estados operacionais vencem o humor: durante a escuta a face mostra
escuta. Mas a escuta é **atenção com prazo**: o xiaozhi deixa o microfone
aberto até ~1 min depois de responder (mediana de 4,7 s quando a pessoa
fala, máximo de 68 s esperando), e uma face presa em escuta escondia o
humor. A proposta de escuta dura ~8 s depois de entrar em escuta ou da
última mudança de atividade de voz (o ATTENTIVE do NB2 tinha 8–15 s); depois
a face relaxa para o humor com o microfone ainda aberto. Como `MOOD` não é transitória, `EnterIdle()` não a remove; ao voltar
ao IDLE as demais propostas caem e a face mostra o humor. A proposta tem TTL
0 e só é reenviada quando a expressão escolhida muda. Perto do temperamento
ela resolve para NEUTRAL, então o comportamento em paz é o atual.

O humor também modula intensidade e ritmo dos LEDs. Pré-requisito: a
duração da expressão no árbitro precisa prevalecer sobre animações de uma
execução (hoje HAPPY por 60 s volta ao IDLE visual em ~2 s enquanto o
árbitro ainda diz HAPPY).

### 3.3 Energia, tédio e circadiano

Dois valores em RAM, de 0 a 1, separados do humor: a energia não empurra a
valência nem a ativação, ela muda o **jeito** de fazer as coisas.

- **Circadiano:** a energia segue um alvo pela hora do dia: 1 das 8 h às
  20 h, desce até 0,15 entre 20 h e 23 h, fica em 0,15 até as 6 h e sobe
  até 1 entre 6 h e 8 h. Sem hora sincronizada (§5.1) o alvo é 0,8: o robô
  não sabe que é noite e nunca dorme sozinho. A energia anda até o alvo
  devagar (constante de 15 min), então a noite chega aos poucos.
- **Acordar:** sair do repouso (conversa, toque) sobe a energia em 0,25 na
  hora, como alguém que foi chamado; depois ela volta a seguir o relógio.
- **Tédio:** cresce no repouso até 1 em 20 min de silêncio, cai em ~1 min
  de conversa ou toque e cai a 30% num instante quando alguém interage.

| Situação | Efeito |
|---|---|
| Energia baixa | olhar e piscar no ritmo da energia (o mais calmo entre ela e o humor) |
| Energia < 0,35 (sonolento) | a face em repouso mostra sonolento; nada de pensar sozinho |
| Energia < 0,25 e 10 min de silêncio | dorme: estado `SLEEPING`, olhos fechados (só a curva de baixo, sem piscar), "Zzz" subindo ao lado da face, sem olhar em volta; toque ou wake word acordam |
| Tédio | pensa sozinho com mais frequência: o intervalo vai de 1,7× (tédio 0) a 0,7× (tédio 1) |

No árbitro, o sonolento é a fonte `ENERGY`, entre o humor e a iniciativa:
o humor (prioridade 2) < energia (3) < iniciativa (4) < tudo o mais.
`self.mood.get` passa a dizer a energia e o tédio.

### 3.4 Arcos

Reações com começo, meio e fim, em vez de uma face que só aparece e some.
Todo arco segue a mesma gramática: **orientação → execução → desfecho**, e
nenhum termina simplesmente parando. Um arco por vez. Conversa, sono e falha
abortam qualquer arco na hora, sem desfecho e sem empurrar o humor.

| Arco | Gatilho | Sequência | Humor |
|---|---|---|---|
| **Birra e perdão** | 3 toques rápidos (tap) em 10 s | bravo olhando um pouco para baixo (1,5 s) → vira o rosto para um lado, bravo, com uma piscada lenta (1,5 s) → volta para a frente e sorri (1 s) | no começo a valência vai a 0,35 abaixo do repouso (se já não estiver mais baixa) e a ativação desce 0,1, para a mágoa aparecer triste e não brava; o perdão é só a face: a mágoa fica até alguém fazer as pazes |
| **Reconciliação** | humor abaixo do repouso (valência 0,15 abaixo do temperamento com o viés) e carinho | olha para a frente, um pouco para cima, triste ("te olha", 0,4 s) → continua olhando, triste, enquanto o carinho segue (1,5 s) → apaixonado com corações se o carinho continua, contente se parou, com piscada lenta (2,5 s) | no desfecho a valência vai a 0,35 acima do repouso e a ativação desce 0,1: fazer as pazes deixa mais feliz que o repouso |
| **Procura** | tédio ≥ 0,9 em repouso, acordado e sem sono, no máximo 1 vez por hora | curioso, olha um pouco para cima (0,5 s) → 2 a 4 olhadas para os lados num padrão (lateral, diagonal ou em volta; nunca o mesmo da vez anterior), 1 s cada → ninguém: olha para baixo, triste, com piscada lenta (1,8 s) | nenhum |

- **Birra:** os toques que a provocam continuam sendo carinhos rápidos
  (felizes) até o terceiro. Durante a birra, novos toques são ignorados;
  depois do perdão, 60 s de intervalo antes de outra. A birra deixa o humor
  abaixo do repouso, então um carinho logo depois vira reconciliação: o
  ciclo provocar → emburrar → consolar → fazer as pazes. Os arcos levam a
  valência a um ponto (não somam um delta), porque na bancada um humor
  alto antes da birra a deixava acima do repouso e o carinho seguinte não
  reconciliava; pelo mesmo motivo não passam pela reatividade.
- **Reconciliação:** se o carinho parar antes do desfecho, ela aborta sem
  mexer no humor. Depois do desfecho, 30 s de intervalo.
- **Procura:** alguém aparecer no meio (toque, conversa) tira o robô do
  repouso e aborta a procura; quem aparece recebe a reação normal. Sem
  câmera, o robô não "acha" ninguém sozinho: o desfecho da procura é sempre
  o suspiro.
- **No árbitro:** fonte `ARC`, prioridade 45, acima do toque (a birra
  vence o feliz do tap e a reconciliação vence o apaixonado do carinho) e
  abaixo de reflexo e falha. A proposta **não é transitória**: a volta de
  `REACTING` para `IDLE` no meio da birra não a corta; quem encerra é o
  próprio arco (ou conversa, sono e falha), e um prazo de segurança cobre um
  fim perdido. Enquanto o arco vence, o olhar e a piscada lenta são os do
  arco, e o olhar em volta (§ olhar autônomo em FACE.md) para.

## 4. Personalidade local

Parâmetros pequenos, com default no firmware (`core/nb_personality`) e
override por um arquivo de texto no SD:

| Chave | Efeito | Padrão | Faixa segura |
|---|---|---|---|
| `temperament_valence`, `temperament_arousal` | ponto de repouso do humor | 0,10 / 0,05 | −0,5 … 0,5 |
| `reactivity` | ganho aplicado aos estímulos | 1,0 | 0,25 … 2,0 |
| `recovery_s` | tempo até o desvio do humor cair a 5% | 120 | 20 … 600 |
| `negative_slowdown` | quantas vezes a mágoa passa mais devagar | 2,0 | 1 … 4 |
| `initiative` | frequência de comportamentos espontâneos no IDLE (etapa 3) | 0,5 | 0 … 1 |

Formato (D-012): INI, `noise/personalidade.ini` no cartão, porque é
editado por uma pessoa no PC (UTF-8, com ou sem BOM, ou UTF-16 como o
"Unicode" do Bloco de Notas). Uma linha `chave = valor` por parâmetro, `#`
começa comentário, o que o arquivo não diz fica no padrão. Valor fora da faixa é preso nela;
número inválido e chave desconhecida são ignorados e contados, para o boot
dizer o que aconteceu. Um arquivo ruim deixa o robô mais calmo ou mais
vivo, nunca estranho.

```
# Noise mais calmo e mais caloroso
temperament_valence = 0.2
reactivity = 0.8
recovery_s = 90
```

Padrões consolidados da memória longa (§5) podem deslocar esses parâmetros
devagar e com limites; isso entra depois que a memória média estiver
estável.

## 5. Memória

| Camada | Conteúdo | Meio | Sobrevive a reboot |
|---|---|---|---|
| Curta | humor, estado, últimos eventos (ring) | RAM | não |
| Média | acontecimentos relevantes que ainda influenciam o comportamento | microSD | sim |
| Longa | perfil, preferências, fatos, padrões consolidados | microSD | sim |

**Média:** cada episódio tem id, quando, tipo, valência associada,
importância inicial e texto curto opcional. A importância **decai com a
idade** (meia-vida por tipo) e é calculada na leitura,
`importância(agora) = inicial × 2^(−idade/meia-vida)`: nada é decrementado no
arquivo, então reiniciar não congela nem acelera o decaimento. Episódios
abaixo de um limiar saem na compactação ou viram contagem num padrão da
memória longa. Os recentes e importantes formam o viés de humor de §3.1
(ex.: muito carinho hoje deixa o dia mais caloroso).

**Longa:** perfil (nome, apelido, idioma), preferências e fatos com origem,
data e contador de uso, e padrões consolidados (horários de interação,
frequência de carinho). Corrigir e esquecer são operações de primeira
classe: esquecer apaga do SD, não marca como oculto.

**Formato dos dados (D-012):** o que o robô grava (perfil, fatos,
preferências, episódios) é JSON com um campo de versão de esquema, dentro
dos slots e quadros com CRC de §5.2. O CRC garante a integridade; o JSON
deixa os dados legíveis no PC, o que serve para depurar e para conferir que
um esquecimento apagou de fato. O volume é pequeno, então um formato
binário não compensa.

**Persistência:** núcleo C17 puro (importância, decaimento, consolidação,
busca por palavra/tag, limites de tamanho, formato e recuperação) + casca
FATFS sobre SDMMC 1-bit (GPIO 6/15/16). A casca é uma task única dona do
cartão; os demais enfileiram. **Sem SD o robô funciona** só com memória curta
e avisa pelo banner.

Implementação: `core/nb_memory`, sobre `core/nb_json` (leitor e escritor
mínimos, para o núcleo não depender de código de terceiros nos host-tests).
Limites: 64 registros longos, 96 episódios, texto de 96 bytes, chave de 24.
Meias-vidas: carinho 6 h, conversa 12 h, mágoa 48 h, nota pedida pela
conversa 7 dias; a compactação apaga o que cai abaixo de 0,05. O viés é
0,05 × Σ(importância × afeto) dos episódios de idade conhecida, preso em
±0,15. Episódios cheios: sai o que menos importa agora (o mais antigo no
empate). Um episódio sobre um registro longo guarda só o id dele (`about`),
nunca uma cópia do texto. Os padrões consolidados ficam para a etapa 3; até
lá, o episódio que sai na compactação não vira contagem.

Casca: `memory/memory_service`, task única dona da memória; os outros só
avisam o que aconteceu (fila sem bloqueio). Arquivos `slot_a/memoria.bin`,
`slot_b/memoria.bin` e `log/episodios.log` em `/sdcard/noise/`. Carinho de 3 s ou
mais vira episódio de carinho (importância 0,3 a 0,7 com a duração, até
30 s); conversa de 5 s ou mais, de sair do repouso até voltar a ele, vira
episódio de conversa com o desvio que deixou no humor (×2, preso em ±1) e
importância de 0,3 a 0,6 com a duração (até 2 min).

No cartão: um snapshot JSON (`schema`, `next_id`, `forgetting` quando há
esquecimento pendente, `records`) nos dois slots e um quadro por episódio no
log (`{"schema":1,"episode":{...}}`). Um snapshot com CRC certo mas que esta
versão não sabe ler (esquema mais novo, por exemplo) trava a memória contra
escrita: nada o empurra para fora dos slots.

### 5.1 Hora

- **Fonte:** o xiaozhi acerta o relógio do sistema (`settimeofday`) com o
  `server_time` da checagem de versão a cada boot com rede (`ota.cc`). Não
  há RTC com bateria, mas o relógio do sistema sobrevive a reset por
  software, então pode já estar acima do piso antes de qualquer
  sincronização. Três níveis, sem patch:
  - **inválida** — `time()` abaixo de um piso fixo (data do build);
  - **plausível** — acima do piso, sem sincronização observada neste boot;
  - **sincronizada** — a casca observou neste boot um salto no deslocamento
    entre `time()` e o relógio monotônico (o `settimeofday` do xiaozhi).
  Só a sincronizada conta como "válida" no resto desta seção; a plausível é
  tratada como inválida para gravar e para o viés de humor.
- **Fuso embutido:** o xiaozhi soma o `timezone_offset` ao timestamp antes de
  gravar, então `time()` é hora local disfarçada de UTC. Idades continuam
  corretas enquanto o fuso não muda; hora do dia sai de `gmtime()`, não de
  `localtime()`. Uma troca de fuso desloca as idades pelo tamanho da troca,
  o que é tolerável para meias-vidas de horas a dias.
- **Testes obrigatórios:** hora abaixo do piso; plausível herdada de reset
  por software; sincronização no meio do boot; hora futura errada seguida de
  correção para trás (no mesmo boot e em boot seguinte); troca de fuso de
  ±1 h a ±14 h sem idade negativa nem perda de validade.
- **Gravação:** todo registro guarda `boot_id` + tempo monotônico e, se a
  hora for válida, também a hora de parede. Quando a hora fica válida no
  mesmo boot, os registros sem parede são convertidos pelo monotônico. Se o
  boot acaba sem hora válida, ficam com **data desconhecida**.
- **Correção dentro do boot:** uma nova sincronização que move o relógio
  além de uma tolerância (para frente ou para trás) refaz a hora de parede
  dos registros deste boot pelo monotônico; a última sincronização vence.
- **Último instante visto:** a casca persiste a hora válida mais recente
  (a cada ordem de 10 min e na compactação). Ele só serve para rebaixar uma
  hora **plausível** que esteja abaixo dele. Uma hora **sincronizada** abaixo
  dele vence: o instante visto antes estava errado (ex.: servidor no futuro),
  é substituído, e registros com hora futura ficam com idade 0 até o tempo
  alcançá-los.
- **Período desligado:** só é conhecido quando chega hora válida; aí a idade
  dos episódios já inclui o tempo desligado, porque é `agora − quando`.
- **Sem hora válida:** recordar, registrar, corrigir e esquecer funcionam;
  episódios de idade desconhecida **não entram no viés de humor**, e a
  consolidação espera hora válida. A idade nunca é negativa (clamp em 0).

Implementação: `core/nb_clock` (níveis, carimbos, recálculo, idade) e
`memory/clock_service` (amostra a cada 1 s; `boot_id` e último instante
visto no NVS até o microSD existir; o instante é gravado na sincronização e
a cada 10 min). Limite conhecido: depois de um reset por software, se o
relógio guardado já estiver a menos de 2 s da hora certa, a sincronização
não produz salto e o boot fica em **plausível**; os registros desse boot
ficam sem hora de parede até a próxima correção.

O circadiano usa a mesma fonte, mas fica para a etapa 3.

### 5.2 Gravação interrompida

O FATFS tem baixa resistência a desligamento abrupto (documentação do
ESP-IDF), e "temporário + renomear" não é atômico nele. O contrato não
depende de rename:

- **Registros longos** (perfil, fatos, preferências, padrões, personalidade)
  em **dois slots** (`long.a`, `long.b`). Cabeçalho: magic, versão de
  formato, geração, `seq` máxima incorporada, tamanho e CRC32. **O CRC cobre
  o cabeçalho inteiro (menos o próprio campo) e o conteúdo**: uma geração ou
  um tamanho corrompido invalida o slot, nunca faz uma versão antiga parecer
  a mais recente. Gravar = escrever o slot de geração menor ou inválido,
  `fflush` + `fsync`, fechar. Carregar = validar os dois e ficar com a maior
  geração válida. O único slot válido nunca é sobrescrito.
- **Episódios** em log de acréscimo com quadros `{magic, tamanho, seq,
  conteúdo, CRC32}`, CRC sobre tamanho, `seq` e conteúdo. Ao carregar, a
  leitura para no primeiro quadro inválido; a próxima escrita grava por
  cima do final rasgado. A compactação grava os episódios vivos num
  slot longo com a `seq` máxima e depois zera o log; se a energia cair no
  meio, quadros com `seq` já contida no slot são ignorados.
- **Falha do cartão** (não monta, erro de E/S, cartão removido): modo
  degradado com só memória curta, aviso no banner e nas respostas MCP
  ("memória indisponível"), nova tentativa de montar periodicamente.
  **Nunca formata sozinho.**
- **Volume de escrita:** episódios entram no log na hora; snapshots longos
  são agrupados (ordem de minutos), exceto registrar, corrigir e esquecer
  pedidos pela conversa, que gravam antes de responder.
- **Verificável:** host-test corrompe e trunca cada byte dos arquivos de
  teste, inclusive geração, tamanho e `seq`, e exige que a carga devolva a
  última geração válida, sem travar e sem promover a anterior; em bancada,
  um ciclo de N cortes de energia durante escrita termina com boot
  recuperando dados consistentes.
- **Implementação:** `core/nb_store` (formatos, sem acesso a arquivo) e
  `memory/store_service` (task única dona do cartão; os outros pedem e
  esperam). Arquivos em `/sdcard/noise/`: `slot_a/<registro>.bin` e
  `slot_b/<registro>.bin` para os slots, `log/<log>` para os quadros,
  `personalidade.ini` sozinho para a personalidade. Cada montagem grava e
  relê um slot em `teste/`, que alterna A/B entre boots.
- **FAT intocada nas gravações:** cortes de energia em bancada estragaram
  um arquivo que o firmware nunca escreve (o `.ini`), porque cada gravação
  recriava o arquivo (mexendo na tabela da FAT e na pasta) e todos ficavam
  na mesma pasta. Por isso slots (128 KB) e log (64 KB) têm tamanho fixo:
  são criados zerados uma vez e depois só sobrescritos no lugar, sem
  alocar nem liberar blocos. O slot fica no começo do arquivo e o cabeçalho
  diz quanto ler; cada quadro do log é seguido de 4 bytes zero, então a
  leitura para logo depois do mais novo, e limpar o log é zerar o começo.
  Cada tipo de arquivo tem pasta própria e os dois slots ficam em pastas
  diferentes: um setor de pasta rasgado só atinge a própria pasta. Os
  arquivos do formato anterior migram para as pastas na montagem. Montagem por
  `esp_vfs_fat_sdmmc_mount`: o driver novo de SD host do IDF 6 ainda não é
  usado pela camada de protocolo SD nem pelo FATFS. Um cartão retirado com
  o robô ligado só é percebido na próxima operação.

### 5.3 Alcance do esquecimento

- Todo registro tem id estável. Todo dado derivado guarda os ids de origem:
  episódios que citam um fato, contagens de padrões, e resumos se um dia
  existirem.
- **Índice de busca só em RAM**, reconstruído no boot; nunca persistido.
- `forget(id)` remove o registro e **todo derivado que o referencia**:
  episódios que citam o fato saem e padrões que o contaram são recalculados.
  `forget(categoria)` faz o mesmo por categoria (fatos, preferências,
  episódios, perfil); `forget(tudo)` volta o robô a "recém-adotado".
- **Só responde sucesso depois que nenhuma cópia válida restou**: os dois
  slots longos regravados sem o dado e o log compactado. A resposta diz
  quantos registros de cada tipo saíram.
- **Esquecimento interrompido:** o primeiro slot regravado leva uma marca de
  esquecimento pendente com os ids apagados (sem o conteúdo). Enquanto a
  marca existir, quadros do log e o outro slot com esses ids são filtrados
  na carga, e o boot **conclui a operação** (regrava o outro slot, compacta o
  log, remove a marca) antes de atender consultas. Antes do sucesso a
  operação pode ficar incompleta; se o slot com a marca se perder, o dado
  pode voltar, e isso é aceito porque o sucesso não foi confirmado. Depois do
  sucesso, nenhum slot nem quadro válido contém o dado. Ids nunca são
  reutilizados.
- **Testes obrigatórios:** corte entre o primeiro e o segundo slot, entre o
  segundo slot e a compactação e durante a compactação; em todos, o boot
  seguinte não mostra o dado e termina sem marca pendente.
- `correct(id)` substitui o conteúdo e grava os dois slots, para um slot
  novo danificado não trazer a versão antiga de volta; contagens não mudam.
  Não há derivados textuais a regravar: episódios apontam para o registro
  pelo id.
- **Como o esquecimento grava:** (1) snapshot sem o dado e com a marca,
  (2) log zerado, (3) snapshot limpo, que cai sobre o slot antigo. A marca
  só sai da RAM depois de (3). O boot que encontra a marca filtra esses ids
  e repete os três passos antes de atender consultas.
- Limite declarado: setores liberados do FAT e o nivelamento de desgaste do
  cartão podem reter bytes antigos fisicamente. O esquecimento é dos
  arquivos, não um apagamento forense. O que a nuvem do xiaozhi guardou da
  conversa não é afetado (§7).

## 6. Integração com a conversa (MCP)

Ferramentas do dispositivo, com descrições ricas para o LLM decidir por elas:

| Ferramenta | Faz |
|---|---|
| `self.mood.get` | estado, humor (e depois energia) e o motivo recente |
| `self.mood.stimulate` | a conversa sinaliza um acontecimento afetivo; delta limitado |
| `self.memory.recall` | busca lembranças por palavra, tipo ou período |
| `self.memory.remember` | registra fato, preferência ou acontecimento |
| `self.memory.correct` | corrige um registro existente |
| `self.memory.forget` | apaga um registro ou uma categoria |

A busca no ESP32 é simples (palavra/tag/período); a interpretação semântica
fica com o LLM.

Implementação: `mcp/memory_tools`. As ferramentas rodam na task do app do
xiaozhi e esperam a memória responder na task dela; sem cartão a resposta
diz que a memória está indisponível, para o LLM não inventar. `recall`
devolve id, tipo, texto, quando (ou "data desconhecida"), há quanto tempo e,
nos episódios, o afeto e a importância agora. `remember` aceita `profile`
(com chave; a mesma chave substitui), `fact`, `preference` (chave opcional)
e `note` (um episódio do tipo nota, com valência). `forget` recebe um id ou
uma categoria, nunca os dois. `mood.stimulate` recebe valência e excitação
de −100 a 100, empurra no máximo 0,25 e tem teto de 0,25 por janela de
habituação.

## 7. Relação com a memória do xiaozhi.me

Visto em bancada (2026-10-26): a memória do agente no xiaozhi.me está
ligada. Pedido de "lembra que...", o LLM gravou nela e não chamou
`self.memory.remember`, e depois respondeu "o que você lembra de mim" com o
que ela guardou. Ela pode ser desligada no console, e fica desligada:
com duas memórias o LLM escolhe a da nuvem, o `forget` do robô não a apaga
e os dados ficariam fora do cartão. Desligada, o servidor avisa o LLM que
"a memória de longo prazo está desligada" e ele passa a recusar também as
ferramentas `self.memory`; o prompt da persona precisa de uma seção
"Memória do corpo" dizendo que a memória do robô funciona mesmo assim e
quando chamar cada ferramenta. Com essa seção, `recall` e `remember` passaram
a ser chamados. A memória local é a do
corpo e não depende disso; as duas podem coexistir. Verificar no console do
agente e registrar aqui.

## 8. Aceitação da memória

Pela conversa, com reinício real entre os passos:

1. registrar um fato → reiniciar → recordar (aparece);
2. corrigir → reiniciar → conferir (versão corrigida, a antiga não aparece);
3. esquecer → reiniciar → confirmar ausência na busca, nos episódios e no
   viés de humor.

O mesmo roteiro roda no host contra o núcleo com arquivos simulados.

## 9. Ordem e escopo

1. Humor + estados comportamentais + memória local mínima (episódios,
   perfil/fatos, MCP de consulta, registro, correção e esquecimento).
2. Arcos (birra e perdão, reconciliação, procura), energia, circadiano e
   iniciativa.
3. Movimento segue em entrega própria, sob `motion_safety`.

Portar os núcleos do NB2 integralmente não é requisito: recuperamos os
comportamentos desejados mantendo a simplicidade do NB3.
