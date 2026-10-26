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
- **Energia** — disposição (sono, tédio): entra na etapa 3, com os arcos (§9).

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

## 4. Personalidade local

Parâmetros pequenos, com default no firmware e override no SD:

| Parâmetro | Efeito |
|---|---|
| Temperamento `(v₀, a₀)` | ponto de repouso do humor |
| Reatividade | ganho aplicado aos estímulos |
| Recuperação | constante de decaimento e fator de assimetria negativa |
| Iniciativa | frequência de comportamentos espontâneos no IDLE |

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

**Persistência:** núcleo C17 puro (importância, decaimento, consolidação,
busca por palavra/tag, limites de tamanho, formato e recuperação) + casca
FATFS sobre SDMMC 1-bit (GPIO 6/15/16). A casca é uma task única dona do
cartão; os demais enfileiram. **Sem SD o robô funciona** só com memória curta
e avisa pelo banner.

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
  leitura para no primeiro quadro inválido e o final rasgado é descartado
  (truncado na próxima escrita). A compactação grava os episódios vivos num
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
- `correct(id)` substitui o conteúdo e regrava os derivados textuais que o
  copiam; contagens não mudam.
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

## 7. Relação com a memória do xiaozhi.me

Não verificado ainda: se a memória do agente no xiaozhi.me está ligada,
o que ela guarda, por quanto tempo e como apagar. A memória local é a do
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
