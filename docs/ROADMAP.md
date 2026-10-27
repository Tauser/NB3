# Roadmap — NoiseBot v3

Etapas, prova de sucesso de cada uma e onde o projeto está. Atualizado a cada
entrega; a fonte da verdade do código é o git (`git log`), aqui fica o
andamento em linguagem de produto.

**Atualizado em:** 2026-10-26 — etapa 2 concluída (vida própria e memória no
microSD, com MCP); etapa 3 começando.

## Visão geral

| Etapa | Entrega | Prova de sucesso | Situação |
|---|---|---|---|
| 0 | xiaozhi em board customizada NB3 na Waveshare N32R16V | conversa em PT-BR agrada; wake word funciona | **concluída** |
| 1 | Firmware NB3 próprio: face, emoções, toque, LED | o robô "vive" e conversa | **concluída** |
| 2 | Vida própria: humor contínuo, estados comportamentais, personalidade e memória local mínima no microSD, com MCP | humor sobrevive ao IDLE; lembra de um fato depois de reiniciar; esquece quando pedido | **concluída**; durações do humor (item 2) em observação no uso |
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
  Depois de ~6 h ligado o sensor ficou preso em "tocado" sem ninguém
  encostar (carinho a cada 4 s, corações sem sair): o baseline não segue a
  deriva enquanto há contato. Contato de mais de 60 s agora vira o novo
  baseline (`NB_TOUCH_STUCK`: sai da face de carinho sem virar episódio).
  Falta ver em uso se o travamento não volta.
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

## Etapa 2 — vida própria e memória local — concluída

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
   alegre, com a face saindo do ciano ~8 s após a resposta. Fica para
   observar no uso, sem bloquear a etapa 2.
3. [x] **Face V2** (D-011, [FACE.md](FACE.md)): só os estados que já
   existem.
   - [x] Formas (`50b11c9`): campo `crescent` na pose (corte curvo por
     baixo ou por cima, interpolável); poses refeitas sem sobrancelha para
     atento, pensando, feliz, animado, curioso, confuso, irritado, surpreso,
     triste e sonolento; carinhoso e envergonhado ganharam animação própria.
     Funcionando na tela, segundo o usuário. Contente ganhou expressão
     própria para o humor em repouso (cápsulas baixas, LED pêssego,
     `37f90d3`), confirmada na tela; o carinhoso fica para o toque.
   - [x] Camada de overlay e ligação aos estados (`3e4a60e`): `core/nb_overlay`
     escolhe um adorno por vez (fala vence tudo) e calcula a animação; o
     codec mede o volume do mic e do alto-falante. Escuta → 7 barras
     irregulares seguindo o mic; pensando → pontos e, após 1 s, a mão do
     usuário no queixo; fala → balão que surge, pontos no ritmo da voz e
     some 0,5 s depois; carinho → corações e bochechas; envergonhado →
     bochechas. 11 host-tests do núcleo. A mão vem do recorte do desenho do
     usuário (`tools/design/svg_to_lvgl.py`; os vetores perdiam as linhas).
     Pensando só depois de 0,7 s de espera (`cb400a0`), para a
     wake word não piscar três faces em um segundo. Validado em bancada:
     escuta com barras, balão de fala, pensando na conexão demorada.
     Pendente do mic: o pensando depois da pergunta depende do VAD local
     voltar a "silêncio", o que o lote atual de INMP441 quase nunca faz (e
     o servidor já começa a resposta ao detectar o fim da fala).
4. [x] **Estados comportamentais** (`core/nb_behavior`,
   `behavior/behavior_modifier`, `930e4b3`): IDLE, ENGAGED (conversa),
   REACTING (toque, até 2 s depois de soltar), SLEEPING (só a partir do
   IDLE; qualquer interação acorda) e FAULT (pegajoso até recuperar), mais o
   modo quieto, que sobrevive a toda transição. Lê o `DeviceState` sem
   duplicar a conversa; a conversa vence o toque. O `BehaviorModifier` é o
   único que chama `EnterIdle`. 10 host-tests, incluindo o invariante: em
   cada caminho de volta ao IDLE o transitório sai e o humor fica.
   Validado em bancada pelo log: carinho e tap (reacting → idle 2,1 s após
   soltar), conversa inteira em engaged → idle. Toque durante a conversa
   só no host-test. Sono e modo quieto ainda não têm quem os acione
   (energia na etapa 3, MCP no item 9).
5. [x] **Personalidade** (`core/nb_personality`): temperamento,
   reatividade, recuperação, lentidão da mágoa e iniciativa num perfil com
   os padrões de hoje; leitor de `chave = valor` tolerante (faixas seguras,
   conta o que aplicou, prendeu, rejeitou e desconheceu), formato em
   VIDA.md §4. O humor recebe o perfil pela board e o boot registra o perfil
   em uso. 9 host-tests. Lido do `personalidade.ini` do microSD desde o
   item 7. Falta usar a iniciativa (etapa 3).
6. [x] **Hora para a memória** (`core/nb_clock`, `memory/clock_service`):
   três níveis (inválida, plausível, sincronizada pelo salto do
   `settimeofday` do xiaozhi), carimbos com `boot_id` + monotônico e hora de
   parede só quando sincronizada, recálculo dos carimbos do boot na
   sincronização ou correção, idade nunca negativa, último instante visto
   no NVS. 10 host-tests com os casos obrigatórios de VIDA.md §5.1 (abaixo
   do piso, plausível após reset, sincronização no meio do boot, futuro
   corrigido no mesmo boot e no seguinte, fuso ±1…±14 h, tempo desligado).
   Visto em bancada: boot 1 → 2, último instante lido de volta, sincroniza
   6–10 s após o boot. Ninguém usa os carimbos ainda: a memória (item 8).
7. [x] **microSD** (`core/nb_store`, `memory/store_service`, `d8fc87c`):
   SDMMC 1-bit nos GPIO 6/15/16, task única dona do cartão, dois slots com
   CRC sobre cabeçalho e conteúdo, log de quadros com `seq` lido até o
   primeiro quadro ruim e cauda rasgada truncada antes de anexar, toda
   escrita sincronizada antes de responder, nunca formata, modo degradado
   com aviso no banner e nova montagem a cada 30 s. 12 host-tests: cada bit
   de cada byte do slot e cada byte do log invertidos, todos os
   truncamentos; o slot mais novo danificado nunca vence. Visto em bancada:
   cartão de 960 MB montado, slot de teste alternando A (geração 1) e B
   (geração 2) entre boots. A personalidade passa a vir do
   `personalidade.ini` do cartão quando existe (visto: 2 chaves aplicadas,
   reatividade 0.60 e recuperação 300 s). Sem cartão: erro logado, nova
   tentativa a cada 30 s; o aviso no boot era coberto pelo "Procurando
   redes Wi-Fi", então ele repete na primeira tentativa e depois a cada
   5 min; na tela ele aparece rápido e os avisos do xiaozhi o cobrem, o que
   ficou aceito assim. Cartão inserido com o robô ligado: montou na
   tentativa dos 30 s e o probe passou; a personalidade do cartão só vale
   no próximo boot. Cortes de energia: ver o teste de bancada no item 8b.
8. [x] **Memória média e longa**: episódios com importância calculada pela
   idade, perfil/fatos/preferências, viés de humor limitado, esquecimento
   que remove derivados (VIDA.md §5, §5.3). Host-test do roteiro de §8.
   - [x] 8a, núcleo (`core/nb_memory`, `core/nb_json`): perfil com chave,
     fatos sem repetição, preferências, episódios com meia-vida por tipo,
     viés de humor puro e limitado, busca sem caixa nem acento, compactação,
     reancoragem na sincronização, esquecimento em três escritas com marca
     pendente, snapshot ilegível trava as escritas. 20 host-tests (14 de
     memória, 6 de JSON) com cartão simulado que perde energia em qualquer
     escrita: roteiro de §8, corte em cada escrita do esquecimento e do boot
     que o conclui, cauda rasgada do log, registros malformados.
   - [x] 8b, casca: `memory/memory_service`, task única dona da memória,
     sobre o `StoreService`; carrega quando o cartão aparece e desliga quando
     ele some. Carinho de 3 s ou mais e conversa de 5 s ou mais (de sair do
     repouso até voltar) viram episódios; a conversa leva o desvio que deixou
     no humor. Viés a cada minuto e logo após a sincronização do relógio;
     reancoragem dos carimbos deste boot; compactação a cada 32 quadros;
     gravação agrupada a cada 5 min. Visto em bancada: conversa de 89 s virou
     episódio (v +0,63, importância 0,52), viés +0,033 no humor; depois de
     reiniciar, o episódio voltou do log e o viés voltou após a sincronização.
     Carinhos de 23 s e 46 s viraram episódios (importância 0,61 e 0,70);
     com eles o viés chegou ao teto de +0,15.
     Teste de cortes (`memory/power_cut_test`, só em build de bancada com
     `-DNB3_POWER_CUT_TEST=1`): memória própria em `cortes_*`, escrita sem
     parar (episódios, fatos, esquecimentos, compactação a cada ~50
     operações), conferida a cada boot. Resultado: 48.600 operações e 975
     compactações sem erro; 7 boots após corte no meio da escrita (4 cortes
     de energia, 3 resets) com 0 falhas: nada rasgado, nenhum esquecimento
     pela metade, último fato confirmado presente e último esquecimento
     confirmado ausente. A memória do robô não foi tocada. A falha do
     dispositivo não vira episódio: hoje ela é erro de rede, não mágoa.
     Mas o `personalidade.ini`, que o firmware nunca escreve, apareceu
     depois com lixo binário de 1 KB: um setor rasgado da FAT ou da pasta
     (todos os arquivos ficavam em `noise/` e cada gravação recriava o
     arquivo) atingiu o vizinho. Correção: cada tipo de arquivo na sua
     pasta (`noise/slot_a/`, `noise/slot_b/`, `noise/log/`, `noise/teste/`;
     o `.ini` sozinho em `noise/`) e slots e log de tamanho fixo (128 KB e
     64 KB), criados uma vez e depois só sobrescritos no lugar; o log
     termina cada quadro com 4 bytes zero e limpar é zerar o começo. Os
     arquivos antigos migram no primeiro boot. O teste de cortes passou a
     conferir também que o `.ini` não muda. Falta repetir o teste de cortes
     com o `.ini` no cartão.
9. [x] **MCP** (`mcp/memory_tools`): `self.mood.get`, `self.mood.stimulate`,
   `self.memory.recall`/`remember`/`correct`/`forget`, com descrições ricas.
   As ferramentas rodam na task do app e chamam a memória na task dela
   (`MemoryService::Query`); sem cartão respondem "memória indisponível".
   `remember` aceita perfil (com chave), fato, preferência e nota (episódio
   com valência); `forget` aceita um id ou uma categoria (perfil, fato,
   preferência, episódio, tudo). `mood.stimulate` empurra no máximo 0,25
   com teto próprio de 0,25 por janela, abaixo do toque. Ganho do viés da
   memória reduzido para 0,05 depois da bancada (3 ou 4 bons momentos já
   levavam o viés ao teto). Visto em bancada: as 6 ferramentas registradas e
   a memória carregando 5 episódios no boot. Roteiro de §8 pela conversa
   (1ª rodada): `mood.get` e `recall` chamados certo; `remember` nunca foi
   chamado (o LLM disse "está gravado" e nada chegou ao cartão) e `forget`
   veio com um id inventado. O nome e o café respondidos vinham da memória
   do agente no xiaozhi.me, que está ligada e compete com a nossa (item 10).
   As descrições de `remember`, `forget` e `recall` foram reforçadas (única
   memória do corpo, nunca dizer que gravou ou esqueceu sem a ferramenta
   confirmar, `recall` antes de `forget`). 2ª rodada, com a memória do
   agente desligada: o LLM recusou tudo ("memória de longo prazo
   desligada"). 3ª rodada, com a seção "Memória do corpo" no prompt da
   persona: `recall` e `remember` chamados certo, e o nome gravado sobreviveu
   ao reinício (1 registro longo e 9 episódios no boot). `correct` e
   `forget` validados pela conversa pelo usuário (sem log). A observar:
   gostos ditos na conversa às vezes viram nota (episódio, desbota em ~7
   dias) em vez de preferência; se repetir, orientar no prompt.
10. [x] Verificar no console do xiaozhi.me o que a memória do agente oferece
   e registrar em VIDA.md §7: existe, compete com a do robô e fica
   desligada; o prompt da persona leva a seção "Memória do corpo". O que ela
   guarda e por quanto tempo não foi verificado, porque desligada não importa.

---

## Etapa 3 — arcos, energia e iniciativa — não iniciada

Comportamentos do NB2 recuperados como modifiers sobre o árbitro, sem portar
os núcleos integralmente.

- [ ] Arcos com desfecho: birra e perdão (taps repetidos), reconciliação
  (humor negativo + carinho), procura com o olhar após tédio.
- [~] **Energia, tédio e circadiano** (`core/nb_energy`,
  `behavior/energy_modifier`, VIDA.md §3.3): energia segue a hora do dia
  (1 de dia, 0,15 de madrugada, rampas às 20–23 h e 6–8 h; 0,8 sem hora
  sincronizada) com constante de 15 min, e sobe 0,25 quando alguém chama o
  robô; tédio cresce no repouso (cheio em 20 min) e cai com companhia. Sem
  mexer no humor: o olhar fica no ritmo mais calmo entre humor e energia;
  sonolento (< 0,35) mostra a face sonolenta em repouso (fonte `ENERGY`,
  prioridade 3; humor desceu para 2) e corta o pensar sozinho; o tédio
  encurta o intervalo dos pensamentos (1,7× a 0,7×); energia < 0,25 com
  10 min de silêncio dorme (`SLEEPING`, olhos quase fechados, sem olhar em
  volta), e toque ou wake word acordam. `self.mood.get` diz energia, tédio,
  sonolento e dormindo. 6 host-tests. Visto em bancada à meia-noite: uma
  conversa até 186 s, dois pensamentos sozinhos (395 s e 1140 s), sonolento
  aos 1370 s (energia 0,35) e dormindo aos 1994 s (0,25). Dormindo ganhou o
  overlay "Zzz" (três Z subindo, pedido do usuário), aprovado na tela. Os
  olhos dormindo pareciam estranhos (crescente sonolento achatado a 12% pelo
  piscar travado); viraram olho fechado de verdade (`nb_avatar_core_set_eyes_closed`:
  só a curva de baixo, altura inteira, sem piscar). Aprovado na tela.
  Falta ver: o acordar com toque e wake word.
- [~] **Idle autônomo guiado pelo humor** (`core/nb_gaze`, FACE.md "Olhar
  autônomo"): a coreografia fixa de 8 olhares saiu. Cada olhar sorteia
  direção (8 mais o centro, nunca uma das duas últimas), alcance, ida e
  tempo parado; ~1 em 4 volta ao centro. Ativação muda ritmo, alcance e
  piscar (calmo: 3,5–7 s parado, piscar 1,5× mais lento; alerta: 1,2–3 s,
  0,8×); valência positiva puxa para cima e para os lados, negativa para
  baixo e com menos centro. Vale no repouso e por cima das poses do humor
  (vencedor do árbitro = humor ou nada); qualquer reação traz o olhar de
  volta ao centro com transição de 0,4 s. O avatar recebe o olhar pronto
  (`nb_avatar_core_set_look`) e o ritmo do piscar. Projeção 3D no lugar do
  2.5D de deslocamentos fixos: olhos sobre uma cabeça esférica (raio 520,
  câmera a 1300, giro a 85% do ângulo da cabeça; reforçado depois da
  primeira olhada na tela) em perspectiva; olhando a 27° para o lado o olho
  de perto vai a ~130% da largura e o de longe a ~36%; para cima ou para
  baixo os olhos andam ~180, achatam ~20% e se inclinam ~7° um para o
  outro. 3D aprovado na tela pelo usuário. As microssacadas continuam. 6 host-tests novos do olhar, 2 do avatar e 1 do árbitro
  (vencedor). Falta ver em bancada: o olhar em repouso não parecer ciclo,
  calmo vs alerta, contente olhando para cima, e a volta ao centro numa
  reação.
- [~] "Pensar sozinho" no repouso, dosado pela iniciativa da personalidade
  (`core/nb_initiative`, `behavior/initiative_modifier`, FACE.md): depois de
  um silêncio em repouso (3,5 min no padrão 0,5; 1 min em 1; nunca em 0) o
  pensando aparece por 4–6 s e some; os seguintes a cada ~7 min (metade a
  uma vez e meia). Sair do repouso interrompe na hora e zera o silêncio.
  Fonte `INITIATIVE` no árbitro (prioridade 4, logo acima do humor). 5
  host-tests (silêncio, dose, interrupção, conversa que nunca deixa, relógio
  dando a volta). Visto em bancada: repouso aos 12 s, pensamento aos 222 s
  (210 s de silêncio) por 4,6 s e volta ao neutro; um carinho depois zerou a
  contagem. Falta ver: se o pensando sozinho, com a frequência padrão,
  parece natural no uso.
- [ ] Padrões consolidados da memória (contagens por tipo e horário dos
  episódios que saem na compactação) e, sobre eles, a personalidade
  ajustando devagar e com limites. Hoje um episódio apagado na compactação
  some sem virar contagem.

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
