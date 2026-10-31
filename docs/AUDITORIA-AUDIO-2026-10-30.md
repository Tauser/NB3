# Auditoria de microfone, wake word, AEC e interrupção — 2026-10-30

> Este documento registra o diagnóstico anterior às correções autorizadas.
> A implementação posterior e as pendências de bancada estão em
> [AUDIO_VALIDATION.md](AUDIO_VALIDATION.md). Os números de linha abaixo
> referem-se ao estado auditado, não ao código após a edição.

## Parecer e escopo

A arquitetura tem boas escolhas, mas ainda não deve ser considerada validada de ponta a ponta. Há defeitos de integração comprovados, um deslocamento da referência do AEC contrário à intenção documentada e lacunas de medição acústica. Recomendo corrigir a integração antes de tentar compensá-la aumentando ganho, alterando o cutoff ou tornando a supressão mais agressiva.

Análise do working tree de `D:\Projetos\NoiseBot-v3`, HEAD `1aeead0`, incluindo as alterações preexistentes não commitadas em ganho/limiar, barge-in, testes e modelo Nexo. Não foram feitas correções de firmware, flash, mudança de configuração nem testes físicos. Os resultados acústicos citados no roadmap são evidência histórica, não medições desta auditoria. O usuário solicitou conhecer a proposta antes de qualquer correção.

Base efetiva: ESP-IDF 6.1, xiaozhi v2.5.2 + patch local, esp-sr 2.4.7. As skills do NoiseBot antigo descrevem outra arquitetura e não são evidência sobre este v3.

Modelo examinado: `firmware/main/wake/nexo.tflite`, SHA-256 `cc80182a7de298d7d62668213c285733ecc91abd6406c676ad7a3962a93bae95`.

## Caminho efetivo do áudio

```text
INMP441, slot esquerdo, palavras I2S de 32 bits a 24 kHz
  -> deslocamento >>12, HPF Butterworth de 150 Hz em float
  -> ganho adicional 0 dB, saturação int16
  -> intercalação [microfone, referência do alto-falante]
  -> resampler de dois canais 24 -> 16 kHz
  -> AFE FD_LOW_COST: AEC + NLP VERYAGGR + VADNet; NS/AGC desligados
       -> microWakeWord Nexo, quando habilitado pelo estado da aplicação
       -> quadros PCM de 60 ms -> filtro de interrupção -> fila de encode
       -> Opus -> servidor

Servidor -> decoder/resampler -> volume digital -> TX I2S -> MAX98357
                                    -> callback on_sent -> histórico do AEC
```

O formato `MR`, int16 intercalado a 16 kHz, corresponde ao contrato do [AFE da Espressif](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/audio_front_end/README.html). Mic e referência passam juntos pelo resampler, o que evita atrasos diferentes introduzidos por dois conversores independentes.

## Achados e correções propostas

### A1 — P1: o pré-áudio da interrupção não cabe na fila seguinte

**Comprovado em reprodução com o BargeInFilter real.** `barge_in_filter.cc:83` envia todos os quadros guardados em um loop. Com 300 ms de confirmação e 180 ms extras, retém até oito quadros de 60 ms e emite esses oito mais o atual: 540 ms. A fila de encode tem capacidade dois (`audio_service.h:41`) e remove o mais antigo quando cheia (`audio_service.cc:589`).

Com o consumidor ocupado durante a liberação, foram emitidos nove quadros e descartados sete: perdem-se até 420 ms, inclusive o começo da fala. O número real de perdas depende do escalonamento; não ocorre necessariamente em toda interrupção. O produtor AFE tem prioridade 3 e o codificador 2, portanto não é válido pressupor que o consumidor drene cada chamada imediatamente.

**Proposta:** dar ao caminho de pré-áudio capacidade explícita para a rajada máxima mais a margem de áudio ao vivo, com consumo em ordem pelo worker Opus e contagem de descartes. Não bloquear a task de fetch do AFE. Uma solução inicial pode reservar capacidade suficiente na fila existente, após calcular SRAM/PSRAM e latência; evitar apenas aumentar a fila sem testar o backlog.

**Aceite:** teste de integração com consumidor pausado durante a abertura, nove quadros preservados na ordem, nenhum descarte no cenário previsto e captura ao vivo continuando sem deadlock. Repetir sob reprodução e rede lenta.

### A2 — P1: o parâmetro chamado de antecipação atrasa a referência

**Aritmética comprovada; impacto acústico precisa de medição.** `nb_spk_ref.c:59` usa `consumed + offset - lead`. Com timelines alinhadas e `lead=120`, fornece `ref[n]=speaker[n-120]`: uma ocorrência do sinal aparece na referência 120 amostras depois, isto é, 5 ms a 24 kHz.

Na reprodução com o núcleo real, um impulso no alto-falante em 480 aparece na referência em 600. Para um eco hipotético de 1 ms, o mic teria o impulso em 504: a referência chega 96 amostras/4 ms depois do eco. Esse 1 ms é uma hipótese do teste, não uma medição da placa. O teste existente `test_lead_moves_reference_earlier` confere o índice histórico, mas não verifica a ordem temporal dos impulsos.

Além disso, `nb_spk_ref_captured()` recalcula o offset em toda callback RX a partir do último contador TX. A ordem/latência das callbacks pode alterar a estimativa por blocos de 240 amostras/10 ms. Compartilhar clock evita deriva de frequência, mas não comprova sozinho a fase absoluta dos contadores de callbacks.

**Proposta:** primeiro registrar mic/ref com índice de amostra e medir o atraso por correlação, sob várias ordens de callbacks, volumes e carga. Depois corrigir a convenção de sinal e o alinhamento. Não inverter o sinal às cegas: a solução precisa garantir disponibilidade da amostra de referência, causalidade e estabilidade entre blocos; pode exigir retenção adicional do mic ou pareamento dos blocos por índice.

**Aceite:** impulso e ruído com atrasos conhecidos, TX/RX em ordens diferentes, reinício, overflow e wrap; depois referência/mic fisicamente capturados e melhora de cancelamento sem destruir a fala simultânea do usuário.

### A3 — P1: os desligamentos por Kconfig não compilam

**Confirmado com o compilador Xtensa e os flags reais do build.** A tradução de `afe_audio_engine.cc` passa com a configuração atual. Com `CONFIG_NB3_MICRO_WAKE_WORD` removido, falha por uso de `MicroWakeWord` incompleto nas chamadas de Start, Stop, FeedMono e no unique_ptr. Com `CONFIG_NB3_BARGE_IN_GATE` removido, falha no unique_ptr de `BargeInFilter`.

Os includes são condicionais, mas nem todos os membros/chamadas/destruições correspondentes são. Isso quebra o rollback anunciado para Jarvis por Kconfig. O fallback em runtime, quando o modelo não carrega e o código microWakeWord continua compilado, é outro caminho e não foi demonstrado quebrado por este teste.

**Proposta:** tornar consistentes includes, membros e chamadas para cada opção. Acrescentar uma matriz de compilação com Nexo ligado/desligado e gate ligado/desligado, além da combinação sem AEC quando aplicável.

**Aceite:** configurações compilam e linkam; boot em bancada identifica o detector correto. A checagem desta auditoria foi de compilação sintática da unidade AFE, não um build/link completo de cada variante.

### A4 — P2: o cache de wake word é preenchido para Nexo, mas não é enviado

**Confirmado no fluxo de código.** `HandleWakeWordResult()` guarda PCM para microWakeWord (`afe_audio_engine.cc:529`), mas `EncodeWakeWordData()` retorna para qualquer detector diferente de WakeNet, após tratar MultiNet (`:620`). `GetWakeWordOpus()` repete essa exclusão. `CONFIG_SEND_WAKE_WORD_DATA=y` está ativo.

A aplicação envia o evento textual de wake word mesmo sem receber pacotes do cache. A detecção local funciona, mas o contrato de envio do pré-áudio não é cumprido para Nexo. Isso reduz a proteção contra perda do início de uma frase colada ao nome. Não afirmar que só habilitar o cache preservará toda a fala durante a conexão: a captura nesse intervalo precisa ser validada separadamente.

**Proposta:** compartilhar a codificação/leitura do cache entre WakeNet e microWakeWord, com término explícito, reset por sessão e tratamento de falha de alocação/encoder.

**Aceite:** disparo Nexo produz pacotes válidos em ordem e termina; nova sessão não envia áudio velho; testar “Nexo, que horas são?” sem pausa, inclusive com abertura lenta do canal.

### A5 — P2: depois de aberto, o gate deixa de usar o nível de voz

**Comprovado com o núcleo real.** Em `nb_barge_gate.c:47`, quando aberto, apenas o booleano VAD controla o timeout. O nível em dBFS não participa. Cinco quadros a −13 dBFS abriram o gate; os cem seguintes a −40 dBFS com VAD positivo passaram, deixando-o aberto por mais seis segundos.

O roadmap já registra eco residual classificado como fala pelo VADNet. Nesse cenário, uma abertura legítima ou falsa pode manter a passagem aberta enquanto houver eco, apesar de ele estar abaixo do limiar de abertura.

**Proposta:** separar critérios de abertura e manutenção com histerese. A manutenção deve considerar evidência de voz do usuário e timeout desde a última evidência válida, preservando pausas e sílabas fracas. O limiar fixo de −24 dBFS não distingue sozinho voz do usuário, TV e outra pessoa; ele mede nível, não identidade nem distância.

**Aceite:** após abertura, eco fraco persistente não mantém o gate indefinidamente; a frase real continua íntegra mesmo com pausas e variação de intensidade.

### A6 — P2: o gate segue o estado lógico de TTS, não o fim físico do playback

**Confirmado no fluxo; ocorrência física não medida.** `application.cc:631` muda de speaking para listening quando recebe `tts stop`. `HandleStateChangedEvent():1009` desarma o gate imediatamente. No modo realtime, pode ainda haver PCM no decoder, na fila de playback ou no DMA.

Assim, a cauda do alto-falante pode passar por um período sem a proteção do gate. O AEC continua sendo a defesa, mas o gate deixa de cobrir exatamente essa cauda. `IsPlaybackIdle()` considera filas e tarefas em andamento; não comprova sozinho o esvaziamento físico de todos os descritores DMA.

**Proposta:** manter o gate associado à atividade efetiva de reprodução, incluindo DMA e uma cauda acústica medida, com transições explícitas para cancelamento e novo turno.

**Aceite:** chegada antecipada de stop, jitter, fila cheia, interrupção e reprodução residual; nenhum áudio antigo é liberado como novo comando e o início da fala do usuário é preservado.

### A7 — P2: a marcação de VAD perde a correspondência nas bordas dos quadros

**Confirmado pela montagem dos buffers.** `HandleVoiceResult():576` combina a decisão VAD em um único `frame_speech_`; `EmitFrame():401` limpa esse booleano. Quando o resultado AFE atravessa a fronteira de 60 ms, as amostras remanescentes permanecem em `output_buffer_`, mas a informação VAD dessas amostras não acompanha o restante.

Exemplo com fetch de 32 ms: 32 ms de silêncio + 32 ms de fala emitem um quadro de 60 ms marcado como fala e deixam 4 ms de fala no buffer. Após limpar o booleano, os próximos resultados silenciosos podem fazer o quadro seguinte sair marcado como silêncio apesar desses 4 ms de fala. Também não é correto interpretar qualquer quadro marcado como 60 ms inteiros de voz contínua.

**Proposta:** carregar a decisão por segmento/amostra até completar cada quadro, ou calcular a persistência no domínio temporal nativo do VAD e manter o PCM separado. Limpar metadados junto com o buffer nos resets.

**Aceite:** sequências com fala começando/terminando nas bordas, alternância voz/silêncio, resets e diferentes tamanhos de fetch/frame.

## Microfone: o que está correto e o que melhorar

- I2S duplex 24 kHz, slot escolhido pelo L/R e resampling conjunto são coerentes no código. O cabeamento real e a integridade elétrica não foram inspecionados nesta auditoria.
- HPF de segunda ordem a 150 Hz antes da saturação é uma boa proteção contra graves que consumiriam headroom. Os oito casos do teste HPF passam. Não elimina chiado de banda larga, voz de TV nem distorção já criada no mic/amplificador.
- “Ganho 0 dB” significa ganho **adicional** zero. O `>>12` aplicado à palavra I2S com conteúdo útil de 24 bits preserva a amplificação digital fixa x16 em relação à conversão usual para int16: aproximadamente +24,1 dB. Não significa defeito por si só, mas exige medir clipping e relação sinal/ruído na escala efetiva.
- O roadmap relata redução de ruído de −15,2 para −22,7 dBFS em TV + ventilador após HPF; o comentário atual cita aproximadamente −40 dBFS em outra condição. Não são medições diretamente comparáveis. Faltam metadados comuns de ambiente, distância, ganho e volume.
- Proposta de instrumentação: RMS, pico, porcentagem saturada, piso de ruído, amostras perdidas e erro/timeout, distinguindo PCM antes/depois de HPF e saída AFE. Guardar somente capturas deliberadas, fora da ISR e da task crítica.
- Comparar uma unidade de microfone de procedência conhecida antes de escolher outro modelo. Segundo mic faz sentido após definir geometria e objetivo de direção/separação; não resolve automaticamente eco nem ruído do lote.

## Wake word: qualidade do modelo e comportamento

As configurações do microfrontend coincidem com `pymicro-features` local: janela 30 ms, passo 10 ms, 40 bandas, limites 125–7500 Hz, redução de ruído, PCAN e escala log. A quantização usa escala e zero point do tensor de entrada; o núcleo tem testes de faixa e arredondamento. A decisão usa média de cinco probabilidades, cutoff 229 com comparação estrita `>`, aquecimento de 34 inferências e log de quase-acertos.

O uso do mesmo frontend é positivo, mas não garante igualdade de distribuição: na placa há HPF, saturação e AEC/NLP antes do modelo. A qualidade precisa ser avaliada com gravações desse caminho real.

O ROC atual informa `frr=0.0396` em 0,99 e `faph=0.187` em 0,55. Não há linha explícita para 0,90; não é possível declarar por esse arquivo uma taxa de falso aceite medida em campo no cutoff usado. A documentação ainda descreve o modelo anterior, com negativos em inglês, enquanto o comentário e os arquivos locais indicam novo treinamento com negativos em PT-BR. Falta um manifesto versionado do modelo com dataset, separação por locutor, frontend, receita, hash e relatório no cutoff real.

O aquecimento após Start cria aproximadamente um segundo de indisponibilidade inicial, além do preenchimento da janela. Isso deve ser considerado em chamadas imediatamente após uma transição. Não remover sem comparar falso disparo e tempo de resposta.

**Detalhe importante de estado:** `CONFIG_WAKE_WORD_DETECTION_IN_LISTENING` está desligado; ao entrar em listening o detector é parado e, em speaking realtime, não é reativado. Nessa configuração, a interrupção de conversa depende de VAD/gate/servidor, não de Nexo. Por isso, o relato “o robô falou o próprio nome 50 vezes sem acordar” não prova, isoladamente, que o filtro textual funcionou: é preciso verificar se o detector estava ativo durante o ensaio.

O bloqueio textual é uma defesa auxiliar. `own_voice_` só sai ao chegar frase posterior sem o nome ou ao executar Start; não está vinculado à reprodução física. Em notificações, a legenda vai para a tela sem chamar `OnAssistantSentence()`. Recomendo unificar essa proteção por sessão de playback caso Nexo deva permanecer ativo nesses estados, com testes de última frase contendo o nome, cancelamento e backlog de áudio.

## AEC: o que preservar e o que medir

A referência é extraída em `on_sent` depois da escala de volume digital, antes de o driver zerar o buffer. Conferi essa ordem no `i2s_common.c` local do IDF 6.1. É uma escolha melhor que presumir que todo PCM enfileirado já foi reproduzido. O histórico e os contadores também têm testes para overflow informado, reinício e wrap.

O modo FD_LOW_COST é apropriado para a finalidade. Já `VERYAGGR` pode atenuar a fala do usuário junto com o eco, conforme a [documentação do AEC](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/acoustic_echo_cancellation/README.html). Depois de acertar o alinhamento, comparar NORMAL, AGGR e VERYAGGR com os mesmos áudios de fala simultânea; escolher por cancelamento **e** preservação da voz.

A referência digital não inclui distorção do amplificador/alto-falante, vibração da carcaça nem clipping do mic. Tratar montagem, isolamento acústico, volume útil e headroom faz parte da solução. NS está desligado; avaliar NS separadamente após a referência estar correta, medindo impacto em reconhecimento e CPU. Não ativar AGC indiscriminadamente: mudaria a calibração do gate.

O histórico de referência fica em PSRAM e é acessado pela ISR. `CONFIG_I2S_ISR_IRAM_SAFE` está desligado, portanto isso não demonstra um acesso ilegal durante cache desabilitado na configuração atual. Porém, colocar apenas `IRAM_ATTR` na callback não tornaria toda a cadeia segura se essa opção fosse ligada: funções C, dados e contexto também precisariam ser auditados. Medir duração da seção crítica e perdas durante operações de flash é mais útil que afirmar um crash não observado.

Se `StartReference()` não alocar memória, apenas registra erro e mantém codec sem referência, embora a aplicação tenha sido configurada para realtime. Recomendo um estado de saúde explícito e uma degradação controlada para conversação sem full duplex quando faltar referência, em vez de confiar apenas no log.

## Validação executada nesta auditoria

1. Configuração, build e CTest dos núcleos host: **30/30 executáveis passaram**. Entre eles, HPF (8 casos), wake_detect (13), spk_ref (10), barge_gate (14).
2. Reprodução adicional usando os núcleos C reais e a classe BargeInFilter real, com headers mínimos de hardware/log para host:

   ```text
   AEC: speaker impulse=480, modeled mic echo (1ms)=504, reference=600 samples
   BargeIn: Voice while speaking (run avg -13 dBFS): sending 540 ms to interrupt
   BARGE burst: emitted=9, dropped=7, retained=2 (consumer busy during flush)
   BARGE after opening: weak VAD-positive frames passed=100/100, open=1 after 6000ms
   ```

3. Compilação sintática da unidade AFE com Xtensa e flags do `compile_commands.json`: atual passou; Jarvis por desativação do microWakeWord falhou; gate desligado falhou. Configuração real permaneceu intacta; variantes usaram um header temporário com `#undef`.
4. Evidências temporárias ficam em `firmware/build-host-tests/audio-audit/`, pasta ignorada pelo Git. Nenhum teste novo foi adicionado à suíte de produção.

Não executados: build/link completo do firmware, gravação da placa, captura de mic, medição de ERLE, reconhecimento em distância, teste com TV nem conversa física. Testes unitários não validam a qualidade acústica do produto.

## Ordem de execução proposta, sujeita à aprovação

1. **Recuperar rollback e pré-áudio:** A3, A4 e A1; testes das variantes e da fila com consumidor lento. Alterações no xiaozhi entram no patch versionado, sem edições avulsas no checkout baixado.
2. **Instrumentar e acertar referência:** A2, contadores de continuidade/overflow e capturas deliberadas de mic/ref/AFE. Só escolher o atraso definitivo com evidência física.
3. **Estabilizar a interrupção:** A5, A6 e A7; sincronizar o gate com playback real, preservar etiquetas VAD e validar histerese.
4. **Calibrar qualidade:** ganho/headroom, NLP e NS, cada variável separadamente; então avaliar o modelo Nexo com corpus real e atualizar manifesto/documentação.

Matriz de bancada proposta: silêncio; TV/conversa sem a palavra; positivos do usuário e outras vozes a 0,5/1/2/3 m; robô sozinho em volumes baixo/médio/alto; usuário falando sobre TTS; pancadas/palmas; mudanças de estado; rede lenta; servos ativos dentro da segurança existente.

Medir: taxa de falso aceite por hora, rejeição por condição, atraso wake/interrupção p50/p95, perda do início da fala, RMS/clipping, atraso mic/ref, cancelamento de eco em segmentos sem voz do usuário, qualidade ASR na fala simultânea, perdas de filas/DMA e menor bloco livre de SRAM. Fixar metas antes do ensaio e registrar os resultados por condição. Uma sessão curta sem falso disparo não demonstra estabilidade de horas em uso real.
