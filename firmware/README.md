# Firmware — NoiseBot v3

Firmware ESP-IDF para a Waveshare ESP32-S3-DEV-KIT N32R16V. **Ainda não há
código** — este documento fixa o toolchain, a estrutura e as regras antes da
primeira linha.

## Toolchain

| Item | Versão | Motivo |
|---|---|---|
| ESP-IDF | **v6.1** | SDK recomendado pelo xiaozhi v2.5.2 e usado no CI dele (`espressif/idf:v6.1`); fallback: v6.0.1, mínimo exigido pelo upstream |
| xiaozhi-esp32 | **v2.5.2** (tag) + patch | release estável mais recente; release construída com IDF 6.1.0 |
| Alvo | `esp32s3` | Waveshare N32R16V (32 MB flash octal, 16 MB PSRAM octal) |

Subir a tag do xiaozhi ou a versão do IDF é decisão registrada em
[docs/README.md](../docs/README.md), conferindo o CI do upstream.

Só APIs de driver novas do IDF 6: `i2s_std`, `gpio`, `ledc`, `rmt`,
`i2c_master`, `uart`.

## Etapa 0 (atual)

O projeto compilado é o próprio xiaozhi v2.5.2, baixado em `xiaozhi-esp32/`
por `fetch_repos.py`. O patch só registra a board `NOISEBOT_V3` e deixa o
CMake buscá-la fora da árvore:

```
firmware/
  boards/noisebot-v3/       board NB3 (config.h, config.json, board .cc)
  patches/xiaozhi-esp32.patch
  sdkconfig.nb3.defaults    board, PT-BR, flash octal 32 MB
  repos.json
  fetch_repos.py
```

No VS Code, abra `NB3.code-workspace` (raiz do repositório): ele aponta a
extensão ESP-IDF para o IDF 6.1, a COM5 e os defaults da NB3. Rode
`fetch_repos.py` antes, porque a pasta do firmware é a baixada. A extensão
monta o ambiente pela instalação do EIM, que precisa incluir o alvo
`esp32s3`.

## Estrutura da etapa 1 (planejada)

Estilo StackChan: o projeto é nosso e compila as fontes do xiaozhi baixadas
por tag; a board NB3 vive neste repositório.

```
firmware/
  main/
    main.cpp        boot mínimo: init do HAL, apps, entrega ao xiaozhi
    board/          board customizada NB3 para o xiaozhi (pinos, áudio, display)
    hal/            casca de hardware (servos, LEDs, toque, áudio, SD)
    core/           núcleos C17 puros — sem FreeRTOS/ESP-IDF, clock e RNG injetados
    behavior/       modifiers (casca fina sobre os núcleos) + árbitro
    mcp/            ferramentas MCP do dispositivo, com descrições ricas
    apps/           modos de uso (agente de IA, avatar, setup)
  tests/            host-tests dos núcleos (CMake + ctest) e utilitários de teste compartilhados
  patches/          patches versionados sobre dependências externas
  repos.json        dependências externas por tag fixa
  fetch_repos.py    baixa dependências e aplica patches — falha alto se o patch não aplicar
```

## Regras

- **Núcleo puro + casca fina.** Toda lógica não trivial vive em `core/`, em
  C17, com clock, RNG e I/O injetados, e tem host-test. A casca só traduz
  hardware e RTOS.
- **`motion_safety` é veto absoluto.** Toda escrita de servo passa por ele;
  stall, temperatura, heartbeat e brownout levam a FAULT pegajoso, que só sai
  com reset explícito.
- **Um dono por recurso.** A task de motion é a única que fala com o
  barramento de servos; MCP e modifiers enfileiram comandos. O lock do LVGL
  protege só a tela.
- **Comportamento propõe, árbitro decide.** Modifiers retornam propostas com
  prioridade; o árbitro resolve conflitos e é o único que escreve no avatar e
  no motion.
- **IDLE é o baseline.** Toda entrada em IDLE limpa estado transitório, com
  host-test cobrindo as transições.
- **Sem singletons na lógica.** `GetHAL()` só na casca; núcleos e modifiers
  recebem dependências por parâmetro.
- **Constantes de hardware num lugar só** (`main/board/config.h`).
- **`-Wall -Wextra -Werror`**, zero warnings no nosso código.
- **Comentário explica o porquê atual**; o histórico fica no git.
- **Sem segredos no código**; credenciais só via provisioning em NVS. CI roda
  scan de segredos.

## Comandos (quando houver código)

Invocação do `idf.py` com os caminhos do IDF 6.1: ver `CLAUDE.md`.

```powershell
python fetch_repos.py
cd xiaozhi-esp32          # etapa 0; na etapa 1 o projeto é firmware/
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

Host-tests:

```powershell
cmake -S tests -B build-host-tests
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure
```
