# Validação do áudio após a auditoria de 30/10/2026

## Estado da implementação

Em 31/10, após relato de baixa taxa de ativação, foi preparada uma variante
para comparar o modelo Nexo anteriormente validado na placa. A opção
`CONFIG_NB3_WAKE_PREVIOUS_MODEL=y` incorpora `nexo_anterior.tflite`; ao
desligá-la, incorpora o modelo novo preservado em `nexo.tflite`. O log de
inicialização identifica `model previous` ou `model new`. O limiar não foi
alterado. Após flash conferido por digest, o log confirmou `model previous` e
o usuário relatou melhora significativa na ativação. Ainda faltam métricas
controladas de acerto, outras vozes/distâncias e falsos disparos em repouso.

As correções abaixo implementam A1 e A3–A7 da
[auditoria](AUDITORIA-AUDIO-2026-10-30.md). Ainda exigem teste físico:

- Fila PCM do encoder dimensionada pelo mesmo contrato do pré-áudio: com
  quadros de 60 ms, 8 guardados + 1 de abertura + margem de 2 = **11 quadros**.
  Continua limitada e não bloqueia a task AFE; sobrecarga além desse orçamento
  ainda descarta o mais antigo e incrementa `encode_drop_count`. A ocupação
  máxima de PCM na fila passa de 3,84 KB para 21,12 KB; os buffers do pré-áudio
  são movidos para ela, não duplicados. Medir SRAM mínima sob carga.
- Nexo usa o mesmo encoder e consumidor de cache que WakeNet. Falhas de
  alocação/criação da task terminam o fluxo com marcador vazio, evitando que
  o consumidor aguarde indefinidamente nesses caminhos.
- Includes completos permitem desligar Nexo/gate por Kconfig. CI verifica
  cinco variantes, sem modificar a configuração principal.
- O gate abre com média de −24 dBFS por 300 ms de quadros VAD positivos;
  depois só renova o hold após 180 ms consecutivos com VAD positivo e nível
  ≥ −25 dBFS. Em quadros de 60 ms são três quadros; pulsos de até dois
  quadros não renovam o contador. Fecha após aproximadamente 1 s sem essa evidência.
  Com quadros de 60 ms, o hold é arredondado para 17 quadros (**1020 ms**).
  Os critérios de manutenção são provisórios e precisam de teste de fala
  fraca, pausa e eco variável em hardware.
- O estado speaking solicita o gate; adicionalmente, a captura mantém a
  proteção enquanto há decode/playback, trabalho em andamento, PCM
  possivelmente pendente no DMA ou cauda de sinal efetivamente enviado.
  O limite pendente é calculado de 6 descritores × 240 amostras / 24 kHz =
  **60 ms**; a cauda acústica é **120 ms provisórios**. `tts stop` não remove
  essa proteção física. Cancelamento descarta filas, mas preserva a proteção
  dos samples que já entraram no DMA.
- `VoiceFrames` mantém PCM e etiqueta VAD juntos nas fronteiras AFE/Opus,
  inclusive no restante que só será emitido no quadro seguinte e nos resets.
- Na transição do fim do playback para escuta, o pré-áudio retido só é
  liberado se houver pelo menos dois quadros consecutivos de fala acima do
  limiar junto à transição, incluindo o quadro atual quando cruza a fronteira.
  Um par antigo separado da transição por silêncio não libera a fila. Um pico isolado
  limitado a um quadro não libera a fila; um mesmo evento acústico pode
  atravessar a fronteira e marcar dois quadros. Fala curta de um único quadro
  pode deixar de ser enviada. Se um quadro de pausa VAD separar o pré-áudio
  da voz no momento da transição, o início retido também é descartado; o
  quadro atual passa normalmente. Testar pausas entre sílabas em bancada.

**A2 continua parcialmente aberto:** o parâmetro foi renomeado de
`AUDIO_AEC_REFERENCE_LEAD` para `AUDIO_AEC_REFERENCE_DELAY`, corrigindo o
contrato temporal; o valor de 120 amostras e o algoritmo de alinhamento foram
preservados. O valor 120 era uma escolha inicial sem medição, não um setup
calibrado. `CONFIG_NB3_AEC_DELAY_SAMPLES` permite testar 0 ou 120; o default
continua em 120 até a comparação física. Não houve inversão de sinal nem
calibração sem medição.
Também foram acrescentados contadores de referência indisponível, amostras
descartadas e variação do offset. Timeout com leitura parcial avança o cursor
da referência pelo áudio efetivamente descartado.

O ganho, HPF de 150 Hz, modelo Nexo, cutoff, NS desligado e NLP VERYAGGR não
foram recalibrados nesta entrega. O bloqueio textual de wake word também não
foi redesenhado; o detector continua desativado durante a conversa realtime
pela configuração atual. A degradação automática para half duplex por falha
de alocação da referência permanece como melhoria separada.

## Regressão local

No ambiente ESP-IDF 6.1 descrito em `CLAUDE.md`:

```powershell
cmake -S firmware/tests -B firmware/build-host-tests
cmake --build firmware/build-host-tests
ctest --test-dir firmware/build-host-tests --output-on-failure
python firmware/tests/test_audio_reference.py -v
python firmware/tests/test_fetch_repos.py -v
```

Ao adicionar núcleos C ao glob do componente, reconfigurar antes de linkar:

```powershell
cd firmware
python C:/esp/v6.1/esp-idf/tools/idf.py reconfigure build
cd ..
python tools/check_audio_variants.py
```

`check_audio_variants.py` usa os comandos reais de compilação para AFE,
serviço de áudio e codec da board. As variantes são atual, Jarvis, gate
desligado, Jarvis sem AEC/gate, diagnósticos ligados e AEC em zero com
diagnósticos. É checagem de sintaxe e
tipos; não substitui link/boot completo de cada configuração alternativa.
Headers e logs são gravados em `firmware/build/audio-variants/`.

Mudanças no xiaozhi são mantidas em `firmware/patches/xiaozhi-esp32.patch`.
`python firmware/fetch_repos.py` verifica a equivalência entre tag + patch e
o checkout usado no build.

## Diagnóstico e captura deliberada para AEC

1. Em build de bancada, habilitar `CONFIG_NB3_AUDIO_DIAGNOSTICS=y`.
   `AudioDiag` aparece a cada cinco segundos: `n`, picos mic/ref e `clips`
   são da janela; `errors`, `missing`, `dropped` e `changes` são acumulados.
   `range` é o menor/maior offset desde a sincronização atual. Os contadores
   uint32 podem dar wrap; comparar deltas. Não há PCM nesses logs.
   `PlaybackCheck count=... max_us=...` informa o maior tempo para consultar
   a fila de playback durante a janela, incluindo a espera pelo mutex. Esse
   valor é observabilidade de bancada, não um limite garantido de latência.
2. Para uma gravação sincronizada, habilitar temporariamente
   `CONFIG_USE_AUDIO_DEBUGGER=y` e configurar
   `CONFIG_AUDIO_DEBUG_UDP_SERVER="IP_DO_PC:8000"`. Esse caminho existente
   envia PCM16 a 16 kHz **antes do AFE**, em ordem `[mic, referência]` quando
   a referência está disponível. Verificar o log de 2 canais antes do teste.
3. Executar o receptor existente a partir de uma pasta de capturas:

   ```powershell
   python D:/Projetos/NoiseBot-v3/firmware/xiaozhi-esp32/scripts/audio_debug_server.py --samplerate 16000 --channels 2
   ```

   Interromper com Ctrl+C para fechar o WAV. Capturar fala do robô sem fala
   humana concorrente, em volumes baixo/médio/alto; evitar tom puro. UDP não
   traz sequência/timestamp neste protocolo: descartes ou reordenação podem
   invalidar a continuidade, mesmo que os dois canais de cada pacote estejam
   pareados. Não usar uma captura com falhas para ajustar o atraso.
4. Analisar várias janelas de dois segundos:

   ```powershell
   python tools/analyze_audio_reference.py CAMINHO/16000_2.wav --start 2 --seconds 2
   ```

   `lag_ms > 0` significa eco no mic **depois** da referência; `lag_ms < 0`
   significa referência atrasada em relação ao eco. A ferramenta recusa
   silêncio/baixa correlação e picos no limite da busca; alta correlação,
   sozinha, não comprova causalidade estável nem bom cancelamento. Testar
   várias janelas, polaridades e ordem/variação de callbacks.
5. Desligar os modos de diagnóstico/captura ao concluir. Os defaults
   permanecem desligados. A gravação mic/ref antes do AFE mede alinhamento;
   **não mede ERLE** sem uma captura adicional da saída processada.

Para offset zero, o núcleo atual entrega `ref[n] = speaker[n-delay]`.
Portanto, aumentar `delay` faz um impulso aparecer mais tarde na referência.
Um resultado negativo não deve ser corrigido simplesmente colocando delay
negativo: isso pode exigir samples ainda não disponíveis. A eventual solução
deve parear os blocos e/ou atrasar o mic, com limites explícitos de histórico.

### Comparação 120 × 0

**Ensaio exploratório em 31/10:** duas imagens foram gravadas na placa
`90:e5:b1:cc:3d:58`, ambas verificadas por hash após gravação. Com 120, uma
resposta teve `Held back ... avg -30 dBFS` e a faixa de offset observada foi
de −240 a 0 amostras. Com 0, em uma resposta houve abertura do gate a
−24 dBFS e posterior registro de −26 dBFS. Havia TV ligada, e houve
saturação do mic; não há captura sincronizada nem controle suficiente para
comparar ERLE ou decidir qual delay é melhor. Em uma sessão foram contadas
12240 amostras descartadas com 120 e 12720 com 0; essas contagens são
acumuladas e precisam ser correlacionadas com o ciclo de captura.

Durante o ensaio com 0, a placa reiniciou e informou `last reset: panic`.
O usuário relatou que o robô terminou normalmente a frase antes de reiniciar;
isso não estabelece quanto tempo decorreu entre o fim da frase e o panic.
O filtro de leitura serial descartou o backtrace, então não há evidência da
causa ou de relação causal com o delay. A imagem com 120 foi restaurada e
verificada após a gravação; A/B interrompido até investigar o panic. Não
promover nenhum valor com base nesse ensaio.

As imagens experimentais foram geradas com uma variável CMake temporária,
removida após o ensaio; novas imagens usam o Kconfig e os diagnósticos
configurados explicitamente. Os binários experimentais ficam em `build-host-tests/audio-audit/` e
não substituem a necessidade de reprodutibilidade e medição controlada.

1. Com o mesmo projeto, modelo de wake word e configuração de áudio, gerar
   um firmware com `CONFIG_NB3_AEC_DELAY_SAMPLES=120` e outro com `=0` pelo
   menu NoiseBot v3. Ativar `CONFIG_NB3_AUDIO_DIAGNOSTICS` nas duas versões.
   Identificar e guardar os binários e seus hashes antes do flash; o
   `sdkconfig` existente prevalece sobre `sdkconfig.defaults`.
2. Repetir a mesma gravação de playback em condições fixas, sem usuário
   falando: mesmo volume, posição, ambiente, duração e aquecimento. Alternar
   a ordem entre as rodadas e repetir; guardar logs e WAVs mic/ref de cada
   versão. Anotar também se houve perda/reordenação UDP.
3. Comparar atraso mic/ref, `offset` e `range`, deltas de `missing` e
   `dropped`, quantidade de aberturas do gate e duração de abertura. O log
   `Held back speech ... loudest run avg` é apenas um indicador parcial:
   aparece para sequências VAD retidas enquanto o gate está fechado e não é
   medição de ERLE. O menor valor isolado não define o melhor AEC.
4. Medir também a qualidade da fala simultânea e o início de frases. Escolher
   um valor apenas após inspeção conjunta das capturas e repetições. A seleção
   de 0 pode zerar amostras de referência ainda indisponíveis; o teste host
   demonstra que mudanças na ordem das callbacks podem repetir/pular blocos.
   Não promover 0 sem observar `missing` e o alinhamento real.

## Medição do caminho até o servidor

Serve para decidir onde uma frase foi cortada. Toda sessão de escuta loga uma
linha `Uplink:`, em qualquer build, com quadros entregues à fila, descartados,
codificados e pendentes, pacotes descartados no envio, pacotes pequenos (DTX),
bytes e tempo de voz segundo o VAD. A soma entregues = descartados + codificados
+ pendentes vale para os quadros da sessão. O áudio do wake word vai por outro
caminho e não entra na conta.

O firmware de bancada também envia por UDP, para o PC, o PCM antes do AFE
(porta 8000) e uma cópia dos pacotes Opus enviados ao servidor (porta 8001). Ele
parte do `sdkconfig` atual e fica em `firmware/build-bench/`, sem tocar no
principal:

```powershell
python tools/audio_capture.py config --host IP_DO_PC
cd firmware
idf.py -B build-bench -D SDKCONFIG=D:/Projetos/NoiseBot-v3/firmware/build-bench/sdkconfig -D IDF_TARGET=esp32s3 build
idf.py -B build-bench -D SDKCONFIG=D:/Projetos/NoiseBot-v3/firmware/build-bench/sdkconfig -p COM5 -b 115200 flash
cd ..
python tools/audio_capture.py capture CAMINHO/DA/SESSAO
```

Guardar também o log serial da sessão. Se nenhum datagrama chegar, conferir se
o firewall do Windows permite UDP 8000 e 8001 para o Python e se o robô está na
mesma rede. O envio extra por Wi-Fi pode alterar o tempo do sistema: um defeito
visto só nesse firmware precisa ser repetido sem ele. Ao terminar, regravar o
firmware normal.

### Roteiro

Silêncio na sala, boca a 40 cm do robô. Chamar "Nexo", esperar a resposta e dizer
uma frase por turno, anotando qual saiu. `…` é uma pausa, contada mentalmente.

| # | Frase | Pausa |
|---|---|---|
| 1 | Qual é a capital da França? | nenhuma |
| 2 | Me conta uma curiosidade sobre o oceano. | nenhuma |
| 3 | Quanto é vinte … mais trinta? | 0,5 s |
| 4 | Eu queria saber … como faz um bolo de cenoura. | 0,5 s |
| 5 | Qual é o maior planeta … do sistema solar? | 1 s |
| 6 | Hoje eu acordei cedo … e fiquei pensando no almoço. | 1 s |
| 7 | Me diz uma coisa … qual é a cor do céu? | 1,5 s |
| 8 | Eu tenho uma pergunta … por que o mar é salgado? | 1,5 s |
| 9 | (fraco, sem sussurrar) Quantos dias tem um ano? | nenhuma |
| 10 | Nexo, que horas são? (colada, sem esperar resposta) | nenhuma |

Depois repetir a 1 m e, por fim, com TV ligada, sempre anotando as condições.

### Como decidir

Para cada frase, comparar o que foi dito, o texto do log (`>> ...`), a linha
`Uplink:`, `turn_NNN.wav` e o trecho de `pre_afe.wav` indicado em `events.csv`.

| Observação | Onde está o corte |
|---|---|
| A voz já está ruim ou inaudível em `pre_afe.wav` | microfone ou ambiente |
| `pre_afe.wav` bom, início ou trechos faltando em `turn_NNN.wav`, `dropped` > 0 | fila do dispositivo |
| `pre_afe.wav` bom, `turn_NNN.wav` com a fala baixa virando silêncio, `small packets` alto | Opus/DTX |
| `turn_NNN.wav` termina logo depois do texto, com a fala continuando em `pre_afe.wav` | fim de turno do servidor |
| `turn_NNN.wav` inteiro e claro, texto errado | reconhecimento do servidor |

`lost in capture` no resumo conta perdas entre o robô e o PC; não são perdas a
caminho do servidor, e a frase afetada não entra na decisão. O envio ao servidor
por UDP continua sem medição de perda: a cópia sai antes do envio.

## Aceite em bancada

| Ensaio | Evidência esperada |
|---|---|
| “Nexo, que horas são?” sem pausa | Evento Nexo, log de cache codificado e começo da frase preservado no ASR |
| Interromper resposta longa | Sem novos descartes de encode no orçamento previsto; fala inteira e latência registrada |
| Só robô falando, volumes variados | Gate não permanece aberto por eco abaixo do limiar de manutenção |
| Stop recebido com áudio enfileirado | Cauda de playback protegida; nenhum trecho antigo vira comando |
| Usuário continua com sílabas fracas | Hold/histerese não cortam palavras; ajustar apenas com registros comparáveis |
| TV, palmas, ventilador | Contabilizar falsos disparos/interrupções por condição e por tempo |
| Vozes e distâncias 0,5/1/2/3 m | Taxas de acerto/rejeição do modelo atual; não extrapolar uma voz para todas |
| Carga e operação prolongada | SRAM mínima, descartes, offset range e reinícios estáveis |
| Jarvis e gate desligado | Build/link completo e boot na variante escolhida antes de depender dela como rollback |

Sem flash/ensaio físico, marcar estes itens como **pendentes**, mesmo com
todos os testes locais aprovados.
