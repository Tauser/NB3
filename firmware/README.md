# Firmware — NoiseBot v3

Firmware ESP-IDF para a Waveshare ESP32-S3-DEV-KIT N32R16V. O projeto é do
NB3: o `main` liga o corpo e inicia o xiaozhi como a mente.

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

## Estrutura

Estilo StackChan: o projeto é nosso e compila as fontes do xiaozhi baixadas
por tag. Pastas marcadas com * ainda não existem.

```
firmware/
  CMakeLists.txt    projeto; usa os sdkconfig.defaults do xiaozhi e depois os nossos
  sdkconfig.defaults  board, PT-BR, Jarvis, flash octal 32 MB, sem auto-update
  main/
    main.cpp        boot mínimo: corpo primeiro, depois Application do xiaozhi
    CMakeLists.txt  fontes NB3 + subconjunto ESP32-S3 das fontes do xiaozhi
    Kconfig.projbuild  inclui o Kconfig do xiaozhi (rsource), sem cópia
    idf_component.yml  cópia literal do manifesto do xiaozhi (conferida pelo fetch)
    board/          board NB3 para o xiaozhi (pinos, codec, display) e fiação das peças
    core/           núcleos C17 puros — sem FreeRTOS/ESP-IDF, clock e RNG injetados
    behavior/       árbitro thread-safe (prioridades num lugar só) e ganchos de estado
    face/           tela do NB3 (LVGL): desenha o avatar que o árbitro escolhe
    mcp/            ferramentas MCP do dispositivo, com descrições ricas
    hal/ *          casca de hardware (servos, LEDs, toque, SD)
  tests/            host-tests dos núcleos (CMake + ctest) e utilitários compartilhados
  patches/          patch versionado sobre o xiaozhi (board no Kconfig, auto-update opcional)
  repos.json        dependências externas por tag fixa + arquivos espelhados
  fetch_repos.py    baixa, aplica patches e confere espelhos — falha alto
```

**Fluxo da face:** o xiaozhi entrega a emoção por `Display::SetEmotion()` e
o estado por `Led::OnStateChanged()`; a board NB3 fornece um display (`face/`)
e um Led (`behavior/state_led`) que transformam isso em propostas ao árbitro.
A ferramenta MCP `self.face.set_expression` propõe como fonte própria. A face
consulta o árbitro a cada quadro (30 fps) e desenha o avatar de
`core/nb_avatar` (portado do NB2: olhos em cápsula, 12 animações, piscar e
coreografia do IDLE). `core/nb_face_link` traduz a expressão vencedora em
animação: ela fica na tela enquanto vencer, e a volta ao IDLE deixa um
gesto curto terminar.

**Ao trocar a tag do xiaozhi:** copiar de novo `main/idf_component.yml` (o
`fetch_repos.py` falha até isso ser feito), comparar o `main/CMakeLists.txt`
do xiaozhi com a lista de fontes em `main/CMakeLists.txt` e regenerar o patch.

**Atualização de firmware pelo servidor fica desligada**
(`CONFIG_FIRMWARE_AUTO_UPGRADE=n`): o servidor do xiaozhi poderia oferecer o
firmware original e apagar o NB3. A checagem de OTA continua para ativação.

No VS Code, abra `NB3.code-workspace` (raiz do repositório). Rode
`fetch_repos.py` antes. A extensão ESP-IDF monta o ambiente pela instalação
do EIM, que precisa incluir o alvo `esp32s3`.

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

## Comandos

Invocação do `idf.py` com os caminhos do IDF 6.1: ver `CLAUDE.md`. Tudo a
partir de `firmware/`.

```powershell
python fetch_repos.py
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 -b 115200 flash monitor
```

CI (`.github/workflows/ci.yml`, todo push e PR, sem filtro de caminho):
host-tests com `-Werror -pedantic`, build do firmware no container
`espressif/idf:v6.1` (inclui `fetch_repos.py`, ou seja, patch e espelhos
conferidos) e `tools/scan_secrets.py` sobre os arquivos versionados.

Host-tests:

```powershell
cmake -S tests -B build-host-tests
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure
```
