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
  depois usa −25 dBFS para manutenção e fecha após aproximadamente 1 s sem
  essa evidência. Com quadros de 60 ms, o hold é arredondado para 17 quadros
  (**1020 ms**). O limiar de manutenção é provisório para bancada.
- O estado speaking solicita o gate; adicionalmente, a captura mantém a
  proteção enquanto há decode/playback, trabalho em andamento, PCM
  possivelmente pendente no DMA ou cauda de sinal efetivamente enviado.
  O limite pendente é calculado de 6 descritores × 240 amostras / 24 kHz =
  **60 ms**; a cauda acústica é **120 ms provisórios**. `tts stop` não remove
  essa proteção física. Cancelamento descarta filas, mas preserva a proteção
  dos samples que já entraram no DMA.
- `VoiceFrames` mantém PCM e etiqueta VAD juntos nas fronteiras AFE/Opus,
  inclusive no restante que só será emitido no quadro seguinte e nos resets.

**A2 continua parcialmente aberto:** o parâmetro foi renomeado de
`AUDIO_AEC_REFERENCE_LEAD` para `AUDIO_AEC_REFERENCE_DELAY`, corrigindo o
contrato temporal; o valor de 120 amostras e o algoritmo de alinhamento foram
preservados. Não houve inversão de sinal nem calibração sem medição.
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
desligado, Jarvis sem AEC/gate e diagnósticos ligados. É checagem de sintaxe e
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
