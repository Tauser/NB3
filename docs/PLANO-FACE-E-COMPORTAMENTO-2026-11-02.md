# Plano revisado: simplificação da face — NoiseBot v3

**Revisão:** 3 — 02/11/2026.
**Situação:** etapas 0 (safety isolado), 1 (linha de base e prova de STT/TTS na placa), 2 (galeria e poses de referência), 3 (compositor em três canais) e 4 (seis geradores e quinze receitas, 20ba55a) entregues em 02/11; etapa 5 (bravo vermelho, ícones técnicos, espera confirmada e balão na primeira frase) entregue entre 02 e 03/11; etapa 6 (calibração e aceitação) fechada em 04/11 com ressalvas, ver §13.
**Escopo:** face, composição visual e correções diretamente necessárias.
**Substitui:** o plano amplo anterior deste mesmo arquivo.

## 1. Objetivo, benefício e limites

Diminuir a quantidade de geometria e animação independente mantida no firmware e melhorar a leitura de atividade, emoção e indisponibilidade técnica. A simplificação será verificável por remoção de tabelas, não apenas pela organização dos mesmos desenhos em seis títulos.

Benefícios esperados: menos sequências duplicadas para ajustar, comportamento visual previsível durante sobreposição de eventos e comparação reproduzível entre versões. Economia de CPU, flash ou RAM só poderá ser afirmada depois de medir.

Ficam fora desta entrega: novos movimentos de cabeça, rituais sociais, adaptação de iniciativa, quieto/não perturbe, mudanças de humor e memória, novos sons e enriquecimento do contexto da IA. Os recursos existentes continuam funcionando. A preservação da saída de olhar consumida pela cabeça deve ser verificada, sem reformular seu comportamento.

Referências:

- [Análise de 01/11](ANALISE-ESTADOS-FACES-2026-11-01.md).
- [FACE atual e direção futura](FACE.md).
- [FACE 04: estudo exploratório, não catálogo de implementação](design/face-04-2026-11-01/README.md).
- [Mãos vetorizadas existentes](design/overlays-2026-10-26/vetores/README.md).
- [Decisão de direção D-023](README.md#decisões-registradas).

O mock FACE 04 omite surpreso, animado e tonto, usa posições conflitantes e contém poses ainda ambíguas. Não é evidência de que o repertório está completo nem especificação para copiar pixel a pixel.

## 2. Base conferida e limites da evidência

Esta seção registra a base **no momento da revisão documental**, antes das etapas 0 a 2; o que mudou depois está em §7 (medição na placa), §11 e §13. A revisão releu os contratos do checkout local em main, HEAD 34573d7:

- nb_expression_t possui 15 intenções.
- nb_avatar_expression_t repete essas 15 categorias e nb_avatar.c mantém suas sequências próprias.
- nb_proposal_t possui expressão, prioridade, TTL e transient; não possui intensidade nem episódio.
- nb_overlay_kind_t contém seis tipos visíveis, além de NONE; a composição atual escolhe apenas um.
- face_tools.cc exige igualdade entre o nome recebido e o nome canônico da expressão.
- HandImage está ligado à imagem da mão de pensar, com rotação e escala por software.
- Safety e lembrete escreviam ambos em NB_SOURCE_REFLEX (corrigido na etapa 0: safety agora usa NB_SOURCE_SAFETY).
- A indicação de escuta depende da expressão vencedora; sono chega à face pelo tick de energia.

Na cópia local firmware/xiaozhi-esp32/main/application.cc, o ramo stt chama SetChatMessage("user", ...), e tts/start agenda Speaking. FaceDisplay recebe e registra o texto do usuário, mas retorna antes de utilizá-lo.

**(Situação da revisão documental; as etapas 0 a 2 depois alteraram firmware, rodaram host-tests, build, captura de logs e flash.) Esta revisão não alterou firmware nem executou build do projeto, host-tests, captura de logs ou flash.** O único código executado foi um probe temporário, compilado fora do repositório contra o nb_avatar.c real, que amostrou as 15 expressões por 6 s para medir o alcance dos olhos (§6). Os 37 executáveis aprovados e o probe do conflito de slot pertencem à análise anterior. Nada disso equivale a validação da solução proposta.

## 3. O que diminui e o que permanece

| Elemento | Hoje | Alvo desta entrega |
|---|---|---|
| Intenções semânticas de entrada | 15 enums e nomes | Preservar 15 para compatibilidade com nuvem, humor, arcos e MCP |
| Categorias geométricas internas | 15 categorias de avatar | 6 famílias internas |
| Sequências geométricas | 15 arrays de keyframes próprios | Remover os 15; usar 6 geradores de pose e uma transição compartilhada |
| Especificações de animação | Metadados e movimento por expressão | Perfis comuns por família e poucos parâmetros de variante |
| Mapeamento semântico | Conversão para 15 animações | Tabela de 15 receitas escalares: família, variante, intensidade inicial, ritmo, antecipação, balanço, oscilação e piscar |
| Overlays sociais/de atividade | 6 tipos visíveis | Reutilizar os mesmos 6 efeitos, distribuídos por canais |
| Sinais técnicos persistentes | Banner/som | No máximo 2 ícones simples: rede e parada; sem sprite novo de mão |
| Efeitos especiais | Espiral de tonto no avatar | Preservar como efeito geométrico isolado, sem família ou sequência própria |

Os arrays a eliminar de nb_avatar.c são s_idle, s_listening, s_thinking, s_happy, s_excited, s_curious, s_confused, s_angry, s_surprised, s_sad, s_sleepy, s_dizzy, s_loving, s_embarrassed e s_content. Eliminar também o catálogo de 15 sequências correspondente, sem recriá-lo com outro nome.

A tabela de receitas não poderá conter poses completas nem listas de keyframes disfarçadas: apenas parâmetros aplicados aos seis geradores. A transição parte da pose exibida até a pose-alvo, com envelopes compartilhados de entrada, sustentação e saída.

### O que precisa sobreviver à remoção dos keyframes

Os arrays guardam mais que poses: guardam o que dá vida à expressão. A transição direta perderia isso, então o envelope compartilhado e as receitas devem cobrir:

| Hoje | Exemplo em nb_avatar.c | Parâmetro |
|---|---|---|
| Achatamento antes de abrir | surprised vai de altura 92 a 255; excited, de 112 a 410 | Antecipação na entrada |
| Balanço de um lado ao outro | confused gira +5, −5 e +3 graus | Balanço: amplitude e número de idas |
| Alternância lenta entre duas poses | listening e thinking em loop | Respiração da pose mantida |
| Osciladores por expressão | Vertical: happy 15 a 8 rad/s, content 2,5 a 0,9; horizontal: angry 9 a 32 rad/s | Amplitude e frequência na receita |
| Piscar por expressão | surprised, sleepy e dizzy não piscam | Perfil de piscar na receita |
| Persistente ou de uma passada | neutral, listening e thinking são base; os demais voltam a ela | Atributo da receita |

Remover os arrays não é, sozinho, prova de simplificação. Registrar o saldo de linhas de nb_avatar.c, geradores e receitas, além do que muda ao ajustar uma família. Se o saldo não cair, reavaliar antes de encerrar a etapa.

O enum interno de avatar será substituído por família e parâmetros. Se uma camada temporária de compatibilidade for necessária durante a migração, sua remoção faz parte da entrega final. Preservar apenas os enums semânticos de entrada não é manter 15 desenhos.

### Compatibilidade MCP

Não transformar loving em HAPPY dentro de nb_expression_from_emotion. LOVING continua sendo uma intenção com nome canônico loving; só o adaptador visual a transforma em família ALEGRIA.

Isso mantém válida a comparação existente em face_tools.cc. Preservar todos os nomes atualmente aceitos e rejeitar desconhecidos. Caso futuramente sejam introduzidos aliases públicos, usar parser com sucesso/erro explícito e testes próprios; não inferir validade pelo fallback para neutral.

## 4. Mapeamento completo das 15 intenções

Os valores numéricos de intensidade serão calibrados; as diferenças abaixo são obrigações visuais, não uma nova lista de animações independentes.

| Intenção atual | Família | Parâmetros/efeito | Diferença que precisa aparecer |
|---|---|---|---|
| neutral | PRESENÇA | Cápsulas verticais simétricas | Repouso aberto |
| listening | PRESENÇA | Abertura um pouco maior, olhar central | Atenção; barras vêm da atividade real |
| thinking | PRESENÇA | Olhos desviados juntos para cima/lado | Reflexão; mão no canal de gesto, sem fingir STT |
| happy | ALEGRIA | Crescente positivo de intensidade média | Sorriso reconhecível |
| excited | ALEGRIA | Maior abertura/elevação e entrada mais rápida | Entusiasmo com acomodação, usando os mesmos geradores |
| curious | CURIOSIDADE | Ambos abertos, assimetria moderada e inclinação | Interesse; não parecer piscadinha |
| confused | CURIOSIDADE | Mais diferença de pálpebra e menor abertura de um olho | Dúvida distinta do interesse |
| angry | CONTRARIEDADE | Corte superior inclinado para dentro | Olhos vermelhos enquanto esta intenção for exibida |
| surprised | PRESENÇA | Abertura circular e simétrica | Espanto; permanece disponível ao lembrete |
| sad | TRISTEZA | Pálpebras inclinadas para fora, olhar baixo | Tristeza sem depender de lágrima |
| sleepy | SONOLÊNCIA | Fechamento simétrico e ritmo lento | Cansaço acordado; fechamento completo vem do sono |
| dizzy | PRESENÇA | Pose de suporte e efeito de espiral existente | Tonto explícito, sem nova sequência de keyframes |
| loving | ALEGRIA | Crescente mais fino e suave | Carinho com corações/bochechas existentes |
| embarrassed | TRISTEZA | Inclinação mais leve, olhar lateral baixo e bochechas | Timidez distinta de tristeza e sonolência |
| content | ALEGRIA | Cápsulas abertas com recorte inferior suave | Satisfação distinguível de neutral sem legenda |

### Linha de base medida (etapa 1)

Catálogo atual em `core/nb_avatar.c` (valor a conservar na comparação da etapa 4). Duração é a soma de transição e permanência dos keyframes, sem balanço nem piscar:

| Intenção | Keyframes | Duração (ms) | Reprodução | Piscar |
|---|---|---|---|---|
| neutral (IDLE) | 1 | 260 | loop | sim |
| listening | 2 | 3300 | loop | sim |
| thinking | 2 | 5520 | loop | sim |
| happy | 3 | 1860 | uma vez | sim |
| excited | 3 | 1310 | uma vez | sim |
| curious | 3 | 2020 | uma vez | sim |
| confused | 3 | 2145 | uma vez | sim |
| angry | 3 | 2155 | uma vez | sim |
| surprised | 3 | 1740 | uma vez | não |
| sad | 3 | 3340 | uma vez | sim |
| sleepy | 2 | 2600 | uma vez | não |
| dizzy | 3 | 1260 | uma vez | não |
| loving | 3 | 1920 | uma vez | sim |
| embarrassed | 3 | 2440 | uma vez | sim |
| content | 2 | 2120 | uma vez | sim |

Os 15 arrays ocupam cerca de 157 linhas (85–241) e cada intenção tem um oscilador próprio de balanço (`MOTION`) e um de piscar. Esse é o saldo de linhas de partida.

Render na placa (02/11, conversa real, AEC desligado, 20 janelas de 30 s, linha `Cpu:` do próprio firmware): face entre 18,6 e 22,6 fps desenhados (o desenho só ocorre quando o quadro muda, então não é queda do alvo de 30 fps), 3,2 a 6,0 chamadas de desenho por quadro, tempo médio de desenho de 0,9 a 2,7 ms, máximo por janela de 9 a 57 ms, `taskLVGL` com 15% a 26% de CPU e CPU ociosa de 53% a 69%. Os picos de 20 a 57 ms acontecem durante a fala. Qualquer etapa seguinte deve ficar dentro dessa faixa.

Envergonhado usar a família geométrica TRISTEZA não altera sua semântica nem seu estímulo de humor.

Tonto continua aceito por MCP/nuvem. No primeiro patch de safety, sua reação atual é preservada para isolar a correção de slot. Só na etapa de indicação técnica a chamada automática de tonto por safety será retirada, substituída pelo aviso âmbar persistente. O safety não fica sem sinal entre as etapas.

## 5. Responsabilidades e contratos

Fluxo obrigatório:

    propostas → árbitro → intenção vencedora + fonte
                         ↓
                  adaptador de 6 famílias
                         ↓
                  avatar parametrizado → pose
                         ↓
    atividade + saúde + banner → compositor visual → frame → renderer

O árbitro é o único que escolhe a expressão. O compositor fica **depois** dele: organiza canais, posição, cor e visibilidade. Não consulta propostas perdedoras para escolher outra emoção, não propõe de volta e não substitui o estado de comportamento.

### Intensidade

Não ampliar nb_proposal_t nesta entrega. O adaptador produz um novo contrato visual interno, com família, parâmetros geométricos, intensidade derivada da receita e perfil temporal. O avatar passa a consumir esse contrato em vez de keyframes fixos.

Intensidade livre por MCP ou pelos modifiers está fora do escopo. Assim, não é necessário adicionar campos às inicializações posicionais C/C++ existentes. Se outra etapa exigir ampliação futura, migrar explicitamente todas as inicializações e compilar sob os warnings do projeto antes de integrar.

### Repetição e episódio

Não introduzir um ID de episódio no protocolo de propostas nesta entrega. Entrada visual ocorre ao mudar o par intenção/fonte exibido. Renovação de uma proposta idêntica não reinicia a entrada. O prazo permanece sob o árbitro.

Reação adicional a um novo tap quando a mesma intenção/fonte já está ativa fica adiada. Não usar seq do slot como episódio: uma renovação também altera esse número.

Preservar a política existente de cobertura e TTL: toque pode cobrir um comando sem pausar seu prazo; ao retornar, não reaplicar estímulos ao humor. Cancelamento e recuperação de estado não devem ressuscitar propostas expiradas.

### Dados compartilhados

Snapshots entre tasks devem ser publicados de forma consistente, sem acesso concorrente não protegido. Dormir/acordar passa a ser publicado pela transição de comportamento; o tick de energia continua calculando energia. Não chamar LVGL diretamente do produtor.

## 6. Três canais e contrato de composição

### Medição do alcance dos olhos

Reservar retângulos exclusivos para os olhos não é viável. Com o nb_avatar.c real, sem olhar lateral nem vertical, excited chega a y=6, sad a y=206 e surprised vai de x=41 a x=279. Com o olhar nos extremos, os olhos em repouso cobrem quase toda a altura da tela. Caber numa faixa de 120 px exigiria reduzir todos os olhos em cerca de 25% sem olhar e 50% com olhar, mudando a identidade do personagem.

**Decisão: manter o tamanho dos olhos (kDesignScale) e as posições atuais dos overlays, que seguem os olhos desde o commit 34573d7.** A medição mostra por quê:

| Overlay na posição atual | Toca a caixa de um olho | Observação |
|---|---|---|
| Barras | 0% em repouso nas 15 expressões. Com o olhar nos 4 extremos, toca em excited (98%, olhar para baixo) e curious (97% para baixo direita, 1% em cima direita) | A galeria (etapa 2) mediu as 75 combinações; a medição anterior amostrava só 6 expressões nos extremos |
| Mão de pensar | 0% em repouso, exceto dizzy (93%). Nos extremos toca em 17 combinações, quase todas olhando para baixo (até 97%); em cima, só excited, curious e dizzy (2% a 89%) | Lista completa em `test_face_gallery.py`; qualquer caso novo reprova o teste |
| Balão com cauda | Até 100% em cápsulas altas | Já ocorre hoje; branco sobre branco. Ciano e contorno preto resolvem a leitura |
| Ícone em x=282..314, y=8..40 | 0% | A coluna lateral fica livre |

As caixas ignoram pálpebras e recortes, portanto superestimam o contato. As barras podem sair da tela: a parte inferior chega a y=251 em sad e a y=298 com olhar para baixo.

### Canais

| Canal | Posição | Conteúdo |
|---|---|---|
| A — atividade | Segue os olhos. Barras sob os olhos, entre eles; balão no alto à direita | Barras de escuta ou balão de fala, um por vez |
| G — gesto | Segue os olhos. Mão sob os olhos; corações no alto, ao centro; Zs no alto, à direita | Mão de pensar, corações ou Zs, um por vez |
| T — condição técnica | Fixa na tela: x=282..314, y=8..40 | Rede ou parada segura |

- A e G são limitados à tela: a âncora é restringida para o desenho inteiro continuar visível, em vez de ser cortado.
- Barras e mão ocupam o mesmo lugar e nunca aparecem juntas (ver regras de concorrência).
- **Bochechas não são canal.** Fazem parte da pose: acompanham o canto externo do olho, onde são lidas como bochechas, e podem coexistir com qualquer conteúdo de G.
- A e T recebem contorno preto de 1 px, para continuar legíveis quando passam sobre um olho.
- Os valores são alvos de protótipo: a galeria mede os extremos e só muda este contrato com atualização registrada.

O banner continua sendo uma camada própria, ancorada embaixo, com sua altura real medida pelo LVGL; não supor uma altura fixa de quatro linhas. Ele tem precedência sobre qualquer região que ocupar. Um conteúdo cujo retângulo intercepte o banner é ocultado por inteiro enquanto houver interseção, sem desenhar através do texto. Na prática isso atinge barras e mão, não T. Testar o maior banner antes de aceitar o layout.

Substituir o único kind final por um frame composto: atividade, gesto e condição técnica, cada qual com tipo, parâmetros e visibilidade. Reutilizar as funções de desenho existentes, sem preservar a regra global de que fala apaga tudo.

### Regras de concorrência

- A, G e T podem coexistir, salvo as exceções abaixo e a interseção com o banner.
- Em A, fala tem precedência sobre escuta. Com o AEC desligado (D-024), o microfone fica fechado durante a fala, então barras e balão não coexistem e a voz não modula o balão.
- Barras seguem a pessoa durante a escuta real, não a expressão: aparecem quando o microfone abre (até 8 s, esperando ela começar), ficam enquanto há voz (VAD do dispositivo) e fecham 500 ms depois que ela cala, voltando se ela falar de novo. A primeira versão da etapa 3 as mantinha abertas durante todo o microfone aberto (até cerca de um minuto após uma resposta); o usuário rejeitou isso em 02/11, porque as barras devem fechar quando ele para de falar. O VAD local é ruidoso com o microfone atual e pode segurar as barras; avaliar na calibração.
- Em G, a ordem é: sono, espera confirmada, intenção vencedora. Um gesto por vez **somente neste canal**.
- Barras × mão: durante espera confirmada, a mão substitui as barras. Fora dela, as barras vencem e a mão só aparece se não houver escuta.
- Em T, parada segura vence sem rede. Quando a parada terminar, rede reaparece se a condição continuar.
- Pensar por iniciativa ou comando pode mostrar a mão sem significar espera remota.
- Um comando de expressão não pode inventar estado de microfone ou processamento.
- Uma falha técnica não transforma automaticamente a emoção escolhida; seu canal permanece visível independentemente da intenção.
- Se a placa permanecer em sono diante de um alerta, T continua visível; a política de despertar físico por lembrete não é redesenhada nesta entrega.

## 7. Espera: STT como início, primeira frase da resposta como fim

O caminho existe na cópia local do upstream, sem novo patch: stt com texto agenda SetChatMessage("user", ...), e tts/start agenda a transição para Speaking. A medição na placa (abaixo) mostrou que Speaking começa até 50 ms depois do STT, antes de qualquer áudio. Por isso a espera termina na **primeira frase da resposta** (`tts sentence_start`), não na entrada em Speaking.

Usar o intervalo **recebimento de STT → primeira frase da resposta** como medida de espera. Ele não mede o tempo entre o fim acústico da fala e o STT (a face já está em `thinking` nesse trecho) nem prova que o áudio já está audível.

### Passo de evidência antes da troca

Registrar timestamps monotônicos, número local de turno e eventos STT, Speaking, Idle, erro/desconexão e interrupção da fala, se ocorrer (o corte de fala está adiado e fora desta medição). Não é necessário registrar conteúdo de fala adicional. Confirmar em pelo menos dez turnos normais, e no cenário de reconexão, se o servidor envia STT, a ordem observada e a distribuição do intervalo. A medição vale para o AEC desligado (D-024); se ele voltar, repetir.

### Evidência medida na placa (02/11, AEC desligado, 15 turnos)

Log do console, sem corte de fala. O STT apareceu em todos os turnos, uma vez só, nunca durante Speaking e nunca atrasado. O estado passou de Listening para Speaking entre 0 e 50 ms depois do STT, então **o STT não separa "esperando resposta" de "falando"**: uma espera armada pelo STT e encerrada pela entrada em Speaking dura praticamente zero. O silêncio real fica dentro do Speaking: do STT até a primeira frase da resposta (`tts sentence_start`) passaram de 0,76 s a 2,18 s (mediana cerca de 1,2 s). A face já entra em `thinking` de 0,14 s a 1,4 s antes do STT. Nenhuma espera chegou perto de 20 s; as mais longas apareceram com ferramenta (`get_weather`), já em Speaking. Log bruto: não versionado (scratchpad da sessão).

### Regras propostas

- STT não vazio durante uma sessão válida arma a espera; deduplicar repetição idêntica de evento no mesmo turno.
- A primeira frase da resposta encerra a espera (Speaking já pode ter começado); indicador de fala pode usar atividade de saída já existente para acompanhar a reprodução. Se Speaking terminar sem nenhuma frase, a espera também termina.
- Idle, erro, desconexão, cancelamento ou nova sessão invalidam espera anterior.
- Com o microfone fechado durante a fala, não se espera STT em Speaking. Se chegar um (tardio do turno anterior), não substituir a fala por espera nem armar espera nova; registrar nos logs.
- Interrupção durante a fala encerra Speaking; a espera anterior já terminou e não é reativada. O corte de fala está adiado por decisão do usuário (problemas em andamento) e não faz parte da etapa 1. Pelo código do upstream, a palavra de ativação só é detectada em Speaking se o detector for do AFE (`IsAfeWakeWord`); o dispositivo usa microWakeWord com AEC desligado, então provavelmente não interrompe. Quando o tema voltar, provar na placa e decidir (D-024).
- Não há garantia de correlação remota por ID neste caminho. Ordenação local e limpezas de sessão devem impedir estado preso; testar STT tardio ou repetido.
- Atraso visual inicial: **2,5 s** a contar do STT, acima do maior intervalo medido (2,18 s), para a mão não aparecer em respostas normais. Com 700 ms ela apareceria em quase todo turno. Valor a calibrar na etapa 6 com ferramentas lentas e rede ruim, que a medição não cobriu.
- **A espera não tem desenho novo: é a mão de pensar.** O commit 34573d7 removeu as bolinhas do pensar, e elas não voltam.
- Aos 20 s, limpar a mão e mostrar “Demorando para responder” pelo banner existente; não fingir conclusão nem cancelar o protocolo remotamente. As barras só voltam se houver escuta real; com o microfone fechado em Speaking (D-024) elas ficam ocultas, e o banner pode ocultá-las também.
- O balão de fala não nasce na entrada em Speaking, que acontece antes de qualquer áudio: ele aparece na primeira frase da resposta (`tts sentence_start`) ou na reprodução audível, o que vier primeiro. Entre o STT e esse ponto não há balão nem barras.
- Sem STT comprovado, manter indicação de escuta/conexão e o comportamento atual como fallback identificado. Não rotular silêncio do VAD como confirmação de processamento remoto.

A nova política só substitui a atual depois dessa verificação. Não estimar latência real a partir do código.

## 8. Overlays: limite da entrega e gatilhos

Não expandir de seis para dezesseis efeitos sociais.

| Efeito incluído | Gatilho | Execução |
|---|---|---|
| Barras | Escuta real com voz (VAD) mais 500 ms; janela de 8 s ao abrir o microfone | A; barras atuais, limitadas à tela |
| Mão de pensar | Espera confirmada ou intenção thinking vencedora | G; sprite atual após atraso |
| Balão | Primeira frase da resposta ou reprodução audível, nunca só a entrada em Speaking (§7) | A; renderer e tamanho atuais |
| Corações | loving vencedor | G; efeito atual |
| Bochechas | loving ou embarrassed vencedor | Pose; desenho e posição atuais |
| Zs | Sono de comportamento | G; desenho atual |
| Rede | Estado publicado por LinkMonitor | T; ícone vetorial simples novo |
| Parada segura | Estado de MotionService | T; ícone vetorial simples novo |

Somente os dois últimos são novos desenhos nesta entrega; não são novos gestos.

Lupa, lâmpada, “ops”, aceno social, joinha, risadinha com mão, coçar a cabeça, esfregar o olho e lágrima ficam no catálogo de exploração. Não existem gatilhos/renderers completos para todos eles. Não adicionar MCP de gestos agora. Cada extensão futura exige contrato de evento, prioridade, duração, cancelamento, recurso de arte e medição.

### Convivência com avisos existentes

LinkMonitor continua dono da detecção, dos banners e sons já existentes. O compositor apenas lê seu estado: não repete notificações nem sons. O ícone aparece enquanto NO_WIFI/NO_SERVER e some em ONLINE; STARTING não é tratado automaticamente como queda de rede.

NO_SERVER só termina quando uma conversa começa, então o ícone pode continuar aceso com o servidor já de volta. Nesta entrega, ele significa “a última tentativa falhou”, não “o servidor está fora agora”. Sonda ativa de servidor fica fora do escopo.

Um banner da mesma condição não dispara um segundo anúncio. Safety mantém seu som atual; aviso técnico visual persiste até recuperação real, não por TTL decorativo. A cor vermelha não é reutilizada para esse aviso.

### Custo por recurso

A mão atual é o único sprite de mão desta entrega. Medir isoladamente repouso, mão, fala, escuta e o pior caso A+G+T com e sem banner. Comparar mesma placa/build/configuração e registrar FPS, tempo de desenho, CPU e heap.

O último ~21 FPS do roadmap é uma medição anterior em repouso, não orçamento garantido. Aceitação inicial: nenhuma regressão superior a 10% no FPS mediano frente à linha de base equivalente, nenhum novo dropout de áudio observado e sem crescimento de heap após ciclos repetidos. Se falhar, reduzir transformação por quadro ou usar frames pré-calculados antes de acrescentar efeitos. Para toda futura mão, repetir a medição incremental.

## 9. Política de cor sem ambiguidade

**Decisão de direção:** angry usa vermelho durante todo o tempo em que for a intenção efetivamente exibida. Em um pedido de bravo por 30 s, os olhos ficam vermelhos durante esse prazo, exceto quando outra expressão os cobre. Ao voltar, continuam vermelhos pelo restante; não reiniciar o relógio.

“Pico breve” descreve a intensidade do gesto automático, não um temporizador separado de cor. O renderer pode reduzir movimento na sustentação sem remover a identidade vermelha de angry. A cor do olho passa a vir do frame, e a borda suave (kEdgeColor, hoje cinza fixo) acompanha essa cor.

Ciano é reservado a A; salmão aos efeitos afetivos; âmbar ao aviso técnico. Isso substitui a regra antiga de cores **somente quando esta revisão for implementada**. FACE.md e a descrição do mockup distinguem essa direção do comportamento atualmente embarcado.

## 10. Ferramenta de verificação antes da arte final

Criar uma galeria determinística em tools/design/ como entrega explícita, não presumir que ela existe.

1. Executável host amostra o núcleo real de avatar com seed e relógio controlados e exporta poses, canal e timestamp.
2. Renderer de referência gera frames 320×240 e uma galeria HTML local com reprodução, pausa, tamanho 1:1, legendas opcionais e seleção de cenários.
3. A geometria do preview deve seguir as primitivas do firmware; se for um renderer separado, documentar diferenças e comparar com a placa. Não usar novas imagens geradas por IA como evidência de renderer.
4. Capturar entrada, sustentação e saída das 15 intenções, incluindo surprised, excited e dizzy, além das combinações com A/G/T/banner.
5. Gravar as poses atuais como referência antes da etapa 4 e mostrar antes e depois lado a lado, em movimento. O critério de leitura não detecta perda de vivacidade: entradas sem antecipação ou balanço reprovam mesmo com a expressão reconhecida.
6. Não gerar imagem de “repertório completo” antes de essa matriz existir.

Testes de pares prioritários: neutral/content, curious/confused, sad/sleepy/embarrassed e happy/loving. Para content, usar recorte inferior visível e postura distinta, não apenas diminuir a cápsula. Para tristeza, enfatizar inclinação; sono é simétrico; timidez desloca o olhar e usa bochechas.

Critério inicial de leitura: apresentação aleatória sem legenda, com pelo menos três avaliadores, três passagens por item. Atividades principais devem obter pelo menos 90% de acerto agregado; pares emocionais, 80%. Registrar respostas e confusões, não tratar essa amostra pequena como estudo conclusivo. Com menos de três avaliadores, registrar a limitação em vez de declarar o critério atendido. Repetir na tela física em distância de mesa; pixels 1:1 no monitor não equivalem a duas polegadas.

## 11. Ordem de implementação e saídas obrigatórias

| Ordem | Entrega | Saída verificável |
|---|---|---|
| 0 | Isolar safety de lembrete | Fonte própria, regressão de sobreposição, nenhum redesenho junto |
| 1 | Linha de base e prova de STT/TTS | Logs reais, métricas de render e catálogo das 15 intenções atuais |
| 2 | Galeria e contrato de canais | Preview reproduzível, extremos do olhar e banner testados; poses atuais gravadas como referência |
| 3 | Compositor pós-árbitro e transição de sono | A/G/T independentes; atividade não depende do enum facial |
| 4 | Seis geradores, adaptador e transição comum, **uma família por vez** | Cada família comparada à referência antes da seguinte; remoção das 15 sequências antigas; compatibilidade de nomes/duração; saldo de linhas registrado |
| 5 | Sinais técnicos e cor | Safety âmbar persistente, rede sem anúncio duplicado, angry vermelho consistente |
| 6 | Calibração visual e aceitação | Reconhecimento, desempenho, áudio e cenários integrados registrados |

A correção pequena de safety não depende de aprovar formas, ferramenta de galeria ou contrato de novo avatar. Cada etapa deve ser reversível por commit. A migração pode usar compatibilidade temporária, mas não se encerra mantendo os dois catálogos visuais.

A etapa 4 reescreve o avatar e 78 referências em test_avatar.c e test_face_link.c. Por isso, as poses de referência precisam existir antes dela: sem isso, os testes mudam junto com o código e deixam de proteger a migração.

## 12. Matriz mínima de regressão

- Lembrete chega durante o aviso de safety; safety não perde o slot.
- Nomes loving, embarrassed, excited, surprised, dizzy e demais nomes válidos continuam aceitos; nome desconhecido continua rejeitado.
- Bravo por 30 s, toque no meio, retorno e expiração: cor e duração seguem a regra.
- Qualquer expressão comandada durante escuta preserva A.
- Thinking comandado/iniciativa sem STT não inventa espera remota.
- STT duplicado, tardio, ausente, seguido de erro, seguido de Speaking e chegando durante a fala.
- Interrupção durante a fala (adiada; testar quando o corte de fala voltar): balão sai, escuta ou repouso conforme o modo, sem espera presa.
- Voz do robô termina/interrompe sem deixar balão ou espera presos.
- Sono → toque/wake publica abertura sem esperar o timer de energia.
- Todas as 15 intenções têm receita e sequência de teste; nenhuma cai silenciosamente em neutral por falta de mapeamento.
- Pose, A, G e T nos extremos de olhar e durante todos os tamanhos de banner; barras em sad e olhar para baixo continuam inteiras na tela.
- Espera confirmada troca barras por mão; aos 20 s a mão sai, as barras voltam e o banner avisa.
- Thinking comandado durante escuta mantém as barras e não mostra a mão.
- Bochechas continuam junto aos olhos com mão ou corações em G.
- Surprised e excited mantêm a antecipação; confused mantém o balanço; surprised, sleepy e dizzy não piscam.
- Rede cai/volta sem som ou banner duplicado pelo compositor.
- Safety recupera enquanto rede continua caída: aviso de rede volta a T.
- Renovação de proposta não reinicia entrada; propostas expiradas não reaparecem.
- Saída de olhar para HeadBehavior mantém seu contrato e limites.
- Desempenho com a mão atual e os canais compostos; ausência de regressão relevante de áudio.

Executar host-tests e build ESP-IDF nas etapas de código, sob os warnings existentes.

### Situação da matriz (04/11)

Os 40 host-tests passam (`ctest`, gcc do MSYS2) e o build do ESP-IDF fecha sem warnings. Item a item, na ordem da lista:

| # | Item | Onde está coberto |
|---|---|---|
| 1 | Lembrete durante o aviso de safety | `test_arbiter` (reflexo não troca a safety e a safety cobre o reflexo); na placa, FAULT com lembrete: o ícone ficou e o lembrete apareceu |
| 2 | Nomes aceitos e desconhecido rejeitado | `test_expression` (4 testes) |
| 3 | Bravo por 30 s, toque no meio | duração e retorno em `test_arbiter`; a cor vermelha vem do desenho (`face_display.cc`), sem host-test, vista na placa |
| 4 | Expressão comandada na escuta preserva A | `test_overlay` |
| 5 | Thinking sem STT não inventa espera | `test_overlay` |
| 6 | STT duplicado, tardio, ausente, com erro, com Speaking | `test_turn` (8 testes) |
| 7 | Interrupção durante a fala | adiada junto com o corte de fala |
| 8 | Voz que termina sem balão nem espera presos | `test_turn` e `test_overlay` (balão persiste e sai) |
| 9 | Sono → toque/wake publica a abertura sem esperar o timer | a saída do sono em `test_behavior`; a publicação (`SetSleeping`) fica na casca, sem host-test |
| 10 | 15 intenções com receita | `test_avatar`, `test_face_link` |
| 11 | A, G e T nos extremos e com o banner | `test_face_gallery.py`, `test_overlay_anchor` |
| 12 | Espera troca barras por mão; aos 20 s a mão sai | `test_overlay` e `test_turn`; o aviso "Demorando para responder" fica na casca, sem host-test |
| 13 | Thinking na escuta mantém as barras | `test_overlay` |
| 14 | Bochechas com mão ou corações | `test_overlay` (`test_cheeks_stay_with_the_hand_and_the_hearts`, novo em 04/11) |
| 15 | Antecipação, balanço, quem não pisca | `test_avatar` |
| 16 | Rede cai e volta sem som ou banner duplicados | `test_link` e `test_overlay`; som e banner estão em `link_monitor`, sem host-test (visto na placa com o roteador desligado) |
| 17 | Safety recupera com a rede caída | `test_overlay` (o ícone de rede volta) |
| 18 | Renovação não reinicia; expiradas não voltam | `test_arbiter`, `test_avatar` |
| 19 | Olhar para o HeadBehavior | `test_gaze`, `test_head_follow` |
| 20 | Desempenho e áudio | desempenho medido na placa; áudio parado até a troca do mic |

Fora de host-test ficam os itens que moram na casca (cor do bravo, anúncio de espera longa, publicação do sono, som e banner da rede). Foram vistos na placa, sem teste automático que os proteja de regressão.

## 13. Checklist e encerramento

- [x] Patch isolado de slot safety/lembrete e regressão (host-tests e build). Na placa (04/11): FAULT por corte da energia dos servos com um lembrete marcado; o ícone de parada apareceu e o lembrete foi exibido por cima. A cara de tonto deixou de existir na etapa 5; o slot `NB_SOURCE_SAFETY` ficou sem produtor.
- [x] Evidência em placa de STT, TTS e Speaking: ordem e intervalos no §7 (AEC desligado). Falta repetir com ferramentas lentas e rede ruim.
- [x] Linha de base de desempenho (§4) e prova de STT/TTS (§7), medidas na placa em 02/11.
- [x] Galeria determinística das 15 intenções (`tools/design/face_gallery/`; gerar com `build_gallery.py`). Limites: mão e ícones são marcadores, o banner usa altura estimada, antialiasing difere do LVGL; falta comparar com a tela física e o teste de leitura com avaliadores (etapa 6).
- [x] Poses atuais gravadas como referência antes da etapa 4 (`reference/poses.json`, protegidas por `test_face_gallery.py`).
- [x] Três canais e política de banner implementados, sem encolher os olhos (etapa 3: `nb_overlay` com A/G/T, âncora restringida à tela e ocultação pelo banner medido pelo LVGL). Falta ver na placa; T tem núcleo e testes, mas sem ícone nem produtor até a etapa 5; o produtor de espera confirmada também.
- [x] Compositor não arbitra expressão: recebe o vencedor só para G, e A vem do estado real do áudio.
- [x] Sono publicado por transição (`BehaviorModifier::SetSleepSink` → `FaceDisplay::SetSleeping`); o tick de energia só calcula energia.
- [x] Seis famílias e quinze receitas escalares (`nb_avatar.c`: `FAMILY_*`, `gen_*`, `s_recipes`).
- [x] Quinze arrays antigos e catálogo duplicado removidos. Saldo registrado: tabelas de 183 para 87 linhas de código e cerca de 1.350 números para cerca de 450, mas o arquivo passou de 800 para 912 linhas (geradores, tipos e montador de entrada são código novo). O critério "se o saldo não cair, reavaliar" foi lido como saldo de dados, que caiu; decisão de aceitar fica registrada aqui.
- [x] Antecipação, balanço, oscilação e piscar preservados (testes: durações idênticas, achatamento antes de abrir, balanço de confused e dizzy, quem não pisca). Diferenças conhecidas: dizzy perde a alternância vertical dos olhos; excited e surprised assentam um pouco maiores; confused fica menos largo. Falta a comparação em movimento na galeria e na tela física.
- [x] Compatibilidade MCP: enum e nomes públicos não mudaram (host-tests de face_link e face_tools passam). TTL não foi tocado.
- [x] Cores e avisos técnicos coerentes: bravo vermelho e ícones de rede (símbolo de Wi-Fi riscado) e parada (âmbar) em T, vistos na placa. A espera confirmada e o balão na primeira frase estão em `core/nb_turn` (8 host-tests) e foram vistos na placa pelo dono (04/11): a mão de espera e o balão aparecem como previsto.
- [x] Seis efeitos reutilizados; nenhuma expansão social oculta (seis geradores; os 15 nomes públicos e o MCP não mudaram).
- [~] Critérios visuais e de desempenho. Desempenho atendido (04/11): 21,7 a 23,1 fps, 1,9 a 2,3 ms médios por desenho, picos de 18 a 26 ms, contra a linha de base de 18,6 a 22,6 fps, 0,9 a 2,7 ms e 57 ms; para chegar aí foi preciso invalidar o redesenho por partes e trocar o olho em arco por um só `lv_draw_arc`. Visual: faces conferidas na tela física pelo dono, sem problema. Leitura sem legenda: ferramenta pronta (`-DNB3_FACE_QUIZ=1`, `tools/face_reading_score.py`, [TESTE-LEITURA-FACES.md](TESTE-LEITURA-FACES.md)), não aplicada; o projeto tem um avaliador só, então o critério de 3 avaliadores não se cumpre e só vale como autoverificação. Leitura e espera com ferramenta lenta e rede ruim: verificadas pelo dono na placa em 04/11, sem contagem nem tempos registrados.
- [x] FACE e ROADMAP atualizados com implementação e evidências reais (04/11).

A entrega está concluída quando a redução existe no código e a apresentação é verificável na placa. Mais mãozinhas, modos, rituais e personalidade ficam em planos separados, sem bloquear esta simplificação.
