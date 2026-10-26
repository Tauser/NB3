# Roadmap — NoiseBot v3

Etapas, prova de sucesso de cada uma e onde o projeto está. Atualizado a cada
entrega; a fonte da verdade do código é o git (`git log`), aqui fica o
andamento em linguagem de produto.

**Atualizado em:** 2026-10-26 — etapa 1 concluída; etapas reordenadas pela
D-010 (vida própria e memória local antes do movimento).

## Visão geral

| Etapa | Entrega | Prova de sucesso | Situação |
|---|---|---|---|
| 0 | xiaozhi em board customizada NB3 na Waveshare N32R16V | conversa em PT-BR agrada; wake word funciona | **concluída** |
| 1 | Firmware NB3 próprio: face, emoções, toque, LED | o robô "vive" e conversa | **concluída** |
| 2 | Vida própria: humor contínuo, estados comportamentais, personalidade e memória local mínima no microSD, com MCP | humor sobrevive ao IDLE; lembra de um fato depois de reiniciar; esquece quando pedido | não iniciada |
| 3 | Arcos, energia, circadiano e iniciativa | reage com sequência e desfecho; desacelera à noite; toma iniciativa sem exagero | não iniciada |
| 4 | Servos SCS + `motion_safety` + ferramentas MCP de movimento (entrega própria) | olha e acena; safety corta com carga real | **núcleos prontos**; falta a parte que toca o hardware |
| 5 | Agenda, timers e comportamentos offline como ferramentas do dispositivo | funciona sem nuvem para o que é local | não iniciada |
| 6 | Corpo/placa carrier com acústica tratada; CoreS3 se AEC fizer falta | só se necessário | não iniciada |

Legenda dos itens: **[x]** feito e validado · **[~]** feito, falta validar em
bancada · **[ ]** a fazer.

---

## Etapa 0 — xiaozhi na placa NB3 — concluída

**Objetivo:** provar que a mente em nuvem (xiaozhi.me) serve ao NB3 antes de
investir no corpo: conversa em PT-BR, latência aceitável e wake word.

**Prova de sucesso:** conversa em PT-BR agrada; wake word funciona. Atingida.

Entregue:

- [x] ESP-IDF 6.1 + xiaozhi-esp32 **v2.5.2** por tag fixa + patch, sem fork
  (`427db88`). `fetch_repos.py` baixa, aplica o patch e falha alto.
- [x] Board NB3: ST7789 2" (SPI2), INMP441 + MAX98357 em I2S duplex
  (39/40/41/42), LED onboard, flash octal 32 MB, PSRAM octal (`427db88`).
- [x] Wake word **"Jarvis"** no lugar de "你好小智" (`e6229ed`).
- [x] Ativação no xiaozhi.me, PT-BR e latência aprovados pelo usuário.
- [x] Atualização automática de firmware pelo servidor **desligada**
  (`CONFIG_FIRMWARE_AUTO_UPGRADE=n`, `2fbea76`): o servidor poderia gravar o
  xiaozhi original e apagar o NB3.
- [x] Diagnóstico do mic: ruído alto do lote de INMP441, dominado por grave
  abaixo de 150 Hz; isolado depois de descartar placa, firmware, pino,
  alimentação, fios, amplificador e ambiente (`50c32da`).
- [x] **Filtro passa-altas** de 150 Hz antes da saturação (`37e79cd`): no
  mesmo ambiente (TV + ventilador), piso de ruído −15,2 → **−22,7 dBFS** e
  amostras estouradas 81 → **3**. Wake word a ~1 m em ambiente ruidoso.

Em aberto (não bloqueia):

- [ ] Trocar o mic por um de outra origem (ICS-43434, SPH0645 ou INMP441 de
  outro vendedor) e medir de novo: deve ampliar o alcance da wake word.
- [ ] Supressão de ruído do AFE do esp-sr (hoje `NS: off`): avaliar depois
  do mic novo.

---

## Etapa 1 — corpo vivo: face, emoções, toque, LED — concluída

**Objetivo:** o NB3 dono do próprio boot e da tela, com o xiaozhi como peça;
a face reage à conversa, ao estado do aparelho e ao toque.

**Prova de sucesso:** o robô "vive" (anima sozinho, reage) e conversa.
Atingida: face animada, reação a conversa, estado, toque e LEDs, validados
em bancada.

### Arquitetura

- [x] **Projeto próprio** em `firmware/` com o xiaozhi como componente
  (`2fbea76`): `main.cpp` mínimo, Kconfig do xiaozhi por `rsource`,
  manifesto espelhado e conferido pelo `fetch_repos.py`, patch de 13 linhas.
- [x] **Árbitro de expressões** (`core/nb_arbiter`, `10b9beb`): fontes
  (estado, emoção, comando MCP, toque, reflexo) propõem com prioridade,
  validade e marcação de transitório; IDLE limpa o transitório. Prioridades
  num lugar só (`behavior/expression_arbiter.h`).
- [x] **Ganchos sem patch:** emoção por `Display::SetEmotion()`, estado por
  `Led::OnStateChanged()` (`7cc0970`).

### Face

- [x] **A tela é do NB3** (`a359156`): o layout do xiaozhi não é criado; as
  chamadas de UI dele são interceptadas.
- [x] **Avatar do NoiseBot 2** portado (`core/nb_avatar`, `7a53497`): olhos
  em cápsula, 12 animações por keyframes, piscar, olhar com mola e
  coreografia do IDLE; renderer em LVGL com a geometria do NB2. Validado
  visualmente pelo usuário ("ficou bom").
- [x] Vocabulário de expressões do NB3 (`core/nb_expression`): 14 expressões
  cobrindo as 21 emoções do xiaozhi.
- [x] Ferramenta MCP **`self.face.set_expression`** para a IA controlar a
  face por 1–60 s (`7cc0970`).
- [x] **Banner de sistema** (`83f8624`, `203bb78`): faixa discreta embaixo
  para código de ativação, configuração do Wi-Fi, erros e avisos. Corrigido
  um travamento por fonte liberada na troca de tema; 3 boots seguidos sem
  pânico. Visto na tela com os avisos de conexão do boot; ativação, Wi-Fi e
  erros aparecem quando acontecem.

### Toque e LEDs

- [x] **Toque corporal** (`core/nb_touch`, GPIO2/TOUCH2, `2f445dc`): baseline
  calibrado no boot e com deriva lenta, histerese e debounce; tap → feliz
  1,5 s, segurar 1,5 s → carinho enquanto segura + 2 s. Validado com a mão.
- [x] **LEDs de humor** (`core/nb_mood`, 2× WS2812 no GPIO21, `b46d69c`): cor
  e efeito por expressão (respirar, pulsar, arco-íris no tonto), transição
  suave, teto de brilho. Teto reduzido 60 → 16 → 6/255 (`61d745a` e o
  `f8b65f9`); 6 aprovado em bancada.

### Qualidade

- [x] **CI** (`aecd6ef`): host-tests com `-Werror -pedantic`, build do
  firmware no container `espressif/idf:v6.1` e scan de segredos, em todo push.
- [x] 9 núcleos C17 puros com host-test: expression, arbiter, touch, avatar,
  mood, scs, motion_safety, banner, hpf.

Validação de fechamento:

- [x] Toque (tap e segurar).
- [x] Brilho dos LEDs em 6/255.
- [x] Banner no boot.

Em aberto (não bloqueia):

- [ ] Ver o banner numa mensagem de erro ou ativação (texto com acento).
- [ ] Medir CPU/FPS da face (hoje a tela inteira é redesenhada a 30 fps;
  otimizar para só a área dos olhos se pesar).

---

## Etapa 2 — vida própria e memória local — não iniciada

**Objetivo:** recuperar no ESP32 a autonomia do Noise sem duplicar a máquina
de conversa do xiaozhi. Spec em [VIDA.md](VIDA.md).

**Prova de sucesso:** em IDLE o robô continua contente ou irritado conforme
o que aconteceu; voltar ao IDLE limpa ações temporárias e preserva o humor;
o roteiro de aceitação de VIDA.md §8 passa (registrar → reiniciar →
recordar → corrigir → reiniciar → conferir → esquecer → reiniciar →
ausência); sem microSD o robô funciona e avisa.

1. [x] **Duração da expressão** acima das animações de uma execução
   (`39568c7`): a expressão vencedora fica na pose final com movimento mais
   calmo enquanto valer; ao voltar ao IDLE, um gesto ainda na primeira
   passada termina antes. `core/nb_face_link` liga árbitro e avatar, com
   host-test conjunto. O comando `self.face.set_expression` passou a ser não
   transitório (`83c6033`): dura o prazo pedido mesmo depois de voltar à
   espera, e toque e falha o encobrem sem parar o relógio. Validado em
   bancada: "cara de bravo por 30 s" durou 30 s com o robô em espera; tap no
   meio encobriu e o bravo voltou; tap completa o gesto e carinho mantém a
   face; LEDs acompanharam a face.
2. [~] **Humor** (`core/nb_affect`, `behavior/mood_modifier`, `0c46685`):
   vetor valência × ativação em RAM, volta ao temperamento (+0,10, +0,05),
   valência negativa 2× mais lenta, habituação por chave (fator próprio,
   0,5 por padrão; teto por janela de 30 s), expressão com histerese entre
   feliz, carinhoso, triste e bravo; fonte `MOOD` (prioridade 3, não
   transitória) no árbitro, abaixo da escuta. Estímulos: emoção da
   conversa, tap, pulsos de carinho a cada 4 s, entrada em escuta e falha,
   numa tabela só. Calibração em bancada (`110c0c7` e seguinte): carinho
   com habituação 0,8 → 0,7 e teto maior, escuta só na entrada e mais
   fraca, recuperação 60 → 120 s e limiares 0,25/0,18 para o humor ficar
   ~30 s. Escuta com prazo de 8 s, renovado pela atividade de voz: depois
   da resposta o xiaozhi fica até ~1 min em escuta e a face ciano escondia
   o humor. Visto: habituação da emoção (1,00 → 0,50 → nova janela) sem
   saturar; feliz (amarelo) e carinhoso (rosa) nas conversas. Falta ver:
   carinhoso ~30 s depois de soltar; feliz ~30 s depois da última resposta
   alegre, com a face saindo do ciano ~8 s após a resposta.
3. [ ] **Face V2** (D-011, [FACE.md](FACE.md)): só os estados que já
   existem.
   - [x] Formas (`50b11c9`): campo `crescent` na pose (corte curvo por
     baixo ou por cima, interpolável); poses refeitas sem sobrancelha para
     atento, pensando, feliz, animado, curioso, confuso, irritado, surpreso,
     triste e sonolento; carinhoso e envergonhado ganharam animação própria.
     Funcionando na tela, segundo o usuário. Contente (cápsula baixa do
     humor) ainda usa o carinhoso.
   - [~] Camada de overlay e ligação aos estados (`3e4a60e`): `core/nb_overlay`
     escolhe um adorno por vez (fala vence tudo) e calcula a animação; o
     codec mede o volume do mic e do alto-falante. Escuta → 7 barras
     irregulares seguindo o mic; pensando → pontos e, após 1 s, a mão do
     usuário no queixo; fala → balão que surge, pontos no ritmo da voz e
     some 0,5 s depois; carinho → corações e bochechas; envergonhado →
     bochechas. 11 host-tests do núcleo. A mão vem do recorte do desenho do
     usuário (`tools/design/svg_to_lvgl.py`; os vetores perdiam as linhas).
     Falta ver na tela e calibrar o volume do mic (−45…−12 dBFS → 0…1).
4. [ ] **Estados comportamentais** (IDLE, ENGAGED, REACTING, SLEEPING,
   FAULT; modo quiet) derivados de `DeviceState` sem duplicar a conversa;
   teste de que toda volta ao IDLE limpa o transitório e preserva o humor.
5. [ ] **Personalidade**: temperamento, reatividade, recuperação e iniciativa
   com default no firmware e override no SD.
6. [ ] **Hora para a memória**: validade por piso de build, registros sem
   hora com `boot_id` + monotônico, último instante visto persistido,
   relógio voltando tratado como inválido (VIDA.md §5.1). Host-test.
7. [ ] **microSD** (SDMMC 1-bit, GPIO 6/15/16): task única dona do cartão,
   dois slots com geração e CRC, log de episódios com quadros e `seq`,
   modo degradado sem formatar (VIDA.md §5.2). Host-test de corrupção e
   truncamento em cada byte; bancada com cortes de energia durante escrita.
8. [ ] **Memória média e longa**: episódios com importância calculada pela
   idade, perfil/fatos/preferências, viés de humor limitado, esquecimento
   que remove derivados (VIDA.md §5, §5.3). Host-test do roteiro de §8.
9. [ ] **MCP**: `self.mood.get`, `self.mood.stimulate`,
   `self.memory.recall`/`remember`/`correct`/`forget`, com descrições ricas.
10. [ ] Verificar no console do xiaozhi.me o que a memória do agente oferece
   e registrar em VIDA.md §7.

---

## Etapa 3 — arcos, energia e iniciativa — não iniciada

Comportamentos do NB2 recuperados como modifiers sobre o árbitro, sem portar
os núcleos integralmente.

- [ ] Arcos com desfecho: birra e perdão (taps repetidos), reconciliação
  (humor negativo + carinho), procura com o olhar após tédio.
- [ ] Energia e tédio; circadiano (requer fonte de hora e fuso explícito).
- [ ] Iniciativa no IDLE dosada pela personalidade, sem somar movimento demais
  à coreografia que o avatar já tem.
- [ ] Padrões consolidados da memória ajustando a personalidade devagar e
  com limites.

---

## Etapa 4 — movimento com safety — núcleos prontos

**Objetivo:** cabeça com pan e tilt (2× Feetech SCS0009 via FE-TTLinker,
UART 17/18), sempre sob o veto do `motion_safety`, comandada pela IA por
ferramentas MCP.

**Prova de sucesso:** olha e acena; o safety corta com carga real.

Entregue:

- [x] **Protocolo SCS** (`core/nb_scs`, `825cd84`): pacotes ping/read/write/
  sync write, leitura de posição, velocidade, carga, tensão e temperatura em
  uma requisição, big-endian confirmado no código da Feetech, tratamento do
  eco do barramento half-duplex. 27 testes.
- [x] **`motion_safety`** (`core/nb_motion_safety`, `d9a1e22`, `2c35bab`):
  FAULT pegajoso com reset recusado enquanto o problema persiste;
  superaquecimento, brownout, sobretensão, travamento por carga sustentada e
  por não chegar no alvo, heartbeat da task e **feedback perdido** (servo que
  parou de responder), checados também dentro do veto; limites de pan/tilt
  recusam (não ajustam em silêncio). 42 testes.

A fazer:

0. [ ] **Tracking sem progresso**: hoje alvos alternados a cada 500 ms com a
   posição parada renovam a janela de tracking indefinidamente e o safety
   fica em OK. Limitar o tempo total sem progresso real da posição, com
   regressão no host-test. Bloqueia considerar o movimento seguro.
1. [ ] **Casca da UART** (`hal/`): UART half-duplex nas GPIO 17/18, retiro
   do eco (`nb_scs_echo_len`), flush antes de cada envio, timeout e retentativa.
2. [ ] **Task de motion**: única dona do barramento; lê feedback dos dois
   servos a cada ciclo, alimenta o safety, faz heartbeat e só escreve goals
   aceitos pelo veto. Em FAULT: torque off ou pose segura, e expressão de
   falha no árbitro (prioridade `kFault`).
3. [ ] **Calibração em bancada**: graus por contagem (0,3125 do StackChan ou
   0,293 da especificação), centro e limites reais de pan e tilt, e os
   limiares novos (tensão 4,5–6,5 V, rastreamento 20 contagens/1,5 s,
   feedback 300 ms).
4. [ ] **Ferramentas MCP de movimento** com descrições ricas: olhar para uma
   direção, acenar com a cabeça, negar, centralizar. Toda escrita passa pelo
   safety; a ferramenta responde ao LLM quando o safety recusa.
5. [ ] **Comportamentos**: olhar acompanhando a coreografia do avatar,
   reação ao toque com movimento.
6. [ ] **Prova com carga real**: segurar a cabeça com a mão e ver o safety
   cortar e travar; reset só depois de soltar.

Pré-requisito do usuário: servos ligados na placa (alimentação separada do
trilho lógico) e ids conhecidos.

---

## Etapa 5 — vida offline — não iniciada

**Objetivo:** o que é local funciona sem nuvem.

- [ ] Relógio e agenda locais (RTC por SNTP + persistência em NVS).
- [ ] Timers e lembretes como ferramentas MCP (a IA cria, o robô executa
  sozinho e avisa por som, face e LED).
- [ ] Comportamentos autônomos no IDLE (sonolência à noite, reação a toque
  sem nuvem) e modo offline explícito quando o Wi-Fi ou o servidor caem.
- [ ] Sons locais no microSD (o SD em si entra na etapa 2).

---

## Etapa 6 — corpo e acústica — não iniciada

- [ ] Placa carrier e case com acústica tratada (mic longe do alto-falante e
  do ventilador, vedação do furo do mic).
- [ ] Decidir AEC: referência por software nesta placa, codec com loopback
  (ES7210/ES8311) ou CoreS3.
- [ ] Dois mics só com geometria fixa definida pelo corpo.

---

## Pendências transversais

- [ ] **`fetch_repos.py` aceita mudança extra no upstream**: se o patch
  reverte limpo, o script retorna sem conferir outras diferenças contra a
  tag. Comparar com a árvore esperada (tag + patch), incluindo staged e não
  versionados, com testes de árvore limpa, patch aplicado, patch + mudança
  extra e patch incompatível.
- [ ] **Falha de `xTaskCreate` silenciosa** em `touch_modifier.cc` e
  `mood_lights.cc`: verificar `pdPASS` e registrar falha explícita.
- [ ] **C17 explícito no build do firmware**: as fontes próprias compilam com
  `-std=gnu23` herdado do IDF; fixar C17 nelas sem mexer no upstream.
- [ ] **AEC com referência por software** (mic e amplificador no mesmo I2S):
  experimento para permitir interromper o robô falando por cima dele.
- [ ] Mensagens de status que hoje só vão para o log (conexão, verificação de
  versão): decidir se alguma merece aparecer no NB3.
- [ ] Workspace do VS Code compila só quando a instalação do IDF 6.1 no EIM
  incluir o alvo `esp32s3` (hoje só tem `esp32p4`; o CLAUDE.md contorna).

## Riscos

Ver [docs/README.md](README.md#riscos-em-aberto): qualidade do mic I2S,
mic único sem AEC, privacidade e dependência do xiaozhi.me.
