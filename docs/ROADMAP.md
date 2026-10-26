# Roadmap — NoiseBot v3

Etapas, prova de sucesso de cada uma e onde o projeto está. Atualizado a cada
entrega; a fonte da verdade do código é o git (`git log`), aqui fica o
andamento em linguagem de produto.

**Atualizado em:** 2026-10-26 — etapa 1 concluída.

## Visão geral

| Etapa | Entrega | Prova de sucesso | Situação |
|---|---|---|---|
| 0 | xiaozhi em board customizada NB3 na Waveshare N32R16V | conversa em PT-BR agrada; wake word funciona | **concluída** |
| 1 | Firmware NB3 próprio: face, emoções, toque, LED | o robô "vive" e conversa | **concluída** |
| 2 | Servos SCS + `motion_safety` + ferramentas MCP de movimento | olha e acena; safety corta com carga real | **núcleos prontos**; falta a parte que toca o hardware |
| 3 | Agenda, timers e comportamentos offline como ferramentas do dispositivo | funciona sem nuvem para o que é local | não iniciada |
| 4 | Corpo/placa carrier com acústica tratada; CoreS3 se AEC fizer falta | só se necessário | não iniciada |

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

## Etapa 2 — movimento com safety — núcleos prontos

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

## Etapa 3 — vida offline — não iniciada

**Objetivo:** o que é local funciona sem nuvem.

- [ ] Relógio e agenda locais (RTC por SNTP + persistência em NVS).
- [ ] Timers e lembretes como ferramentas MCP (a IA cria, o robô executa
  sozinho e avisa por som, face e LED).
- [ ] Comportamentos autônomos no IDLE (sonolência à noite, reação a toque
  sem nuvem) e modo offline explícito quando o Wi-Fi ou o servidor caem.
- [ ] microSD (SDMMC 1-bit, GPIO 6/15/16) para sons e dados locais, se fizer
  sentido.

---

## Etapa 4 — corpo e acústica — não iniciada

- [ ] Placa carrier e case com acústica tratada (mic longe do alto-falante e
  do ventilador, vedação do furo do mic).
- [ ] Decidir AEC: referência por software nesta placa, codec com loopback
  (ES7210/ES8311) ou CoreS3.
- [ ] Dois mics só com geometria fixa definida pelo corpo.

---

## Pendências transversais

- [ ] **AEC com referência por software** (mic e amplificador no mesmo I2S):
  experimento para permitir interromper o robô falando por cima dele.
- [ ] Mensagens de status que hoje só vão para o log (conexão, verificação de
  versão): decidir se alguma merece aparecer no NB3.
- [ ] Workspace do VS Code compila só quando a instalação do IDF 6.1 no EIM
  incluir o alvo `esp32s3` (hoje só tem `esp32p4`; o CLAUDE.md contorna).

## Riscos

Ver [docs/README.md](README.md#riscos-em-aberto): qualidade do mic I2S,
mic único sem AEC, privacidade e dependência do xiaozhi.me.
