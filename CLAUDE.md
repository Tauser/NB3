# CLAUDE.md — NoiseBot v3

Docs em PT-BR; código, identificadores e comentários em inglês. Visão e
decisões: `README.md`, `firmware/README.md`, `docs/README.md`.

## Regras de engenharia

- **Núcleos C17 puros** (sem FreeRTOS/ESP-IDF, clock e RNG injetados) com
  host-test; casca fina por cima.
- **`motion_safety` sob toda escrita de servo** (FAULT pegajoso, heartbeat,
  temperatura, brownout). Nenhum caminho de código fala com servo sem passar
  por ele; FAULT só sai com reset explícito.
- **Árbitro central:** modifiers propõem, o árbitro decide e é o único que
  escreve no avatar e no motion. **IDLE é o baseline** e limpa estado
  transitório.
- **Um dono por recurso:** a task de motion é a única que fala com o
  barramento de servos; o lock do LVGL protege só a tela.
- Dependências por parâmetro, **sem singletons na lógica**.
- Só APIs de driver novas do IDF 6 (`i2s_std`, `gpio`, `ledc`, `rmt`,
  `i2c_master`, `uart`).
- CI: `-Wall -Wextra -Werror`, host-tests e scan de segredos.
- Comentários explicam o porquê atual; o histórico fica no git. Utilitários de
  teste compartilhados num lugar só (`firmware/tests/`).
- Ferramentas MCP do dispositivo com descrições ricas (o LLM decide por elas).

## Upstream: tag + patch

xiaozhi-esp32 **v2.5.2** e demais dependências entram por tag fixa em
`firmware/repos.json` + patch versionado em `firmware/patches/`, nunca fork.
`fetch_repos.py` falha alto se um patch não aplicar. Mudança no upstream =
editar o patch, nunca o diretório baixado. Subir tag é decisão registrada.

Referências de consulta (não copiar código sem pedido): `D:\Projetos\Noisebot2`
(núcleos, face, motion_safety) e `D:\Projetos\StackChan` (integração xiaozhi,
modifiers, servos SCS, MCP).

## Ambiente (Windows, ESP-IDF 6.1)

O IDF 5.5.4 em `C:\esp\v5.5.4\esp-idf` pertence a outro projeto: **não
alterar**. O IDF 6.1 foi instalado pelo EIM (manifesto em
`C:\Espressif\tools\eim_idf.json`). Versões de ferramentas exigidas pelo 6.1
saem de `C:\esp\v6.1\esp-idf\tools\tools.json`.

```
IDF_PATH            = C:\esp\v6.1\esp-idf
IDF_PYTHON          = C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe
IDF_PYTHON_ENV_PATH = C:\Espressif\tools\python\v6.1\venv
IDF_TOOLS_PATH      = C:\Espressif\tools
IDF_CMAKE           = C:\Espressif\tools\cmake\4.0.3\bin
IDF_NINJA           = C:\Espressif\tools\ninja\1.12.1
IDF_XTENSA          = C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin
```

`IDF_TOOLS_PATH` é obrigatório: sem ele o component manager falha com erro de
CMake de corpo vazio. `ESP_IDF_VERSION` (major.minor) também: sem ele o
component manager quebra na inicialização do `idf.py` com `TypeError ...
NoneType`. A ativação do EIM para o 6.1 não põe o toolchain xtensa no PATH;
sempre incluir `IDF_XTENSA` explicitamente.

Etapa 0: o projeto compilado é o próprio xiaozhi em `firmware/xiaozhi-esp32`
(baixado por `fetch_repos.py`), com a board de `firmware/boards/noisebot-v3`
e os defaults de `firmware/sdkconfig.nb3.defaults`.

```powershell
$venv = "C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe"
$env:IDF_PATH            = "C:\esp\v6.1\esp-idf"
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\tools\python\v6.1\venv"
$env:IDF_TOOLS_PATH      = "C:\Espressif\tools"
$env:ESP_IDF_VERSION     = "6.1"
$xtensaBin = "C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin"
$env:PATH  = "C:\Espressif\tools\cmake\4.0.3\bin;C:\Espressif\tools\ninja\1.12.1;$xtensaBin;$env:PATH"
$env:SDKCONFIG_DEFAULTS  = "sdkconfig.defaults;../sdkconfig.nb3.defaults"
Set-Location "D:\Projetos\NoiseBot-v3\firmware\xiaozhi-esp32"
& $venv "C:\esp\v6.1\esp-idf\tools\idf.py" <comando>
```

`SDKCONFIG_DEFAULTS` só vale ao gerar o `sdkconfig` (`set-target` ou
`fullclean`); o `sdkconfig.defaults.esp32s3` entra sozinho pelo alvo.

## Hardware e flash

- Placa: Waveshare ESP32-S3-DEV-KIT N32R16V, porta **COM5** (CH343),
  MAC **`90:e5:b1:cc:3d:58`**.
- **Nunca flashear a Freenove CAM** (COM12, MAC `20:6e:f1:b2:3c:f4`): roda
  outro firmware em produção. Antes de qualquer flash, confirmar porta e MAC.
- **GPIO47/48 são domínio 1.8 V**: nunca recebem lógica 3.3 V.
- Pinout em `docs/README.md#hardware`; constantes de hardware só no
  `config.h` da board (`firmware/boards/noisebot-v3/` na etapa 0).
- Servos: nenhum teste de movimento sem `motion_safety` ativo; nunca
  desabilitar limites, heartbeat ou FAULT para "testar rápido".

## Processo

Commits pequenos e frequentes, sem fases burocráticas. Decisão nova entra na
tabela de `docs/README.md`.
