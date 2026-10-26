# Documentação — NoiseBot v3

Documentação curta por regra: cada decisão cabe em meia página, e o roadmap
cabe em uma.

## Decisões registradas

| # | Decisão | Motivo |
|---|---|---|
| D-001 | A **conversa** é do **xiaozhi.me** (nuvem) | ASR, LLM, TTS e persona prontos; robô sempre online e sem setup. Humor, personalidade e memória do corpo ficam no dispositivo (D-010) |
| D-002 | **Nenhum serviço local** — nada roda fora do robô e da nuvem | o robô é um aparelho: liga na tomada e funciona |
| D-003 | Hardware base **Waveshare ESP32-S3-DEV-KIT N32R16V** + display ST7789 2", INMP441 + MAX98357, servos SCS0009, WS2812, toque, microSD | 32 MB flash e 16 MB PSRAM folgados para xiaozhi + face; GPIOs livres para UART de servos, LEDs, toque, I2C e SD; INMP441 + MAX98357 já suportados pelo xiaozhi. CoreS3 fica como upgrade opcional (AEC, touch, bateria) |
| D-004 | **Servos de barramento com feedback** (Feetech SCS) | safety usa carga e temperatura reais, não proxy de corrente |
| D-005 | **ESP-IDF v6.1** (fallback v6.0.1) | SDK recomendado e usado no CI do xiaozhi v2.5.2; v6.0.1 é o mínimo do upstream e entra só se a build falhar no 6.1 |
| D-006 | xiaozhi **v2.5.2** por **tag fixa + patch**, sem fork | release estável mais recente (2026-10-11), construída com IDF 6.1.0; upstream intacto e mudanças auditáveis num arquivo |
| D-007 | Comportamentos como **modifiers** sobre **núcleos C puros**, com **árbitro** | composição no estilo StackChan, com lógica testável no host e um único escritor por saída |
| D-008 | **`motion_safety` obrigatório** sob toda escrita de servo | veto absoluto: FAULT pegajoso por stall, temperatura, heartbeat ou brownout |
| D-009 | Processo leve: roadmap de uma página, decisões de meia página, commits pequenos, CI com testes | energia vai para o robô, não para a burocracia |
| D-010 | **Vida própria no ESP32**: humor contínuo, temperamento, estados comportamentais, reflexos e iniciativa no firmware; memória de médio e longo prazo no **microSD**, acessível pelo firmware e por MCP. O xiaozhi segue dono da conversa e dos estados operacionais | recupera a autonomia do Noise (v2) sem serviço no PC; o SD armazena, o ESP32 executa a lógica. Spec: [VIDA.md](VIDA.md) |

Cada decisão nova entra nesta tabela; se precisar de mais contexto, ganha um
arquivo `D-XXX-<nome>.md` com contexto, decisão e consequências.

## Hardware

Placa: Waveshare ESP32-S3-DEV-KIT N32R16V (módulo WROOM-2, flash e PSRAM
octais).

| Módulo | Sinal → GPIO | Periférico |
|---|---|---|
| Display ST7789 2" | CS 10 · MOSI 11 · SCLK 12 · DC 14 · sem RST/BL | SPI2 |
| Áudio (INMP441 + MAX98357) | Mic SD 39 · BCLK 40 · WS 41 · Spk DIN 42 | `i2s_std` duplex |
| Servos SCS0009 (FE-TTLinker) | TX 17 · RX 18 | `uart` |
| WS2812 externos | 21 | `rmt` |
| LED onboard (WS2812) | 38 | `rmt` |
| Toque corporal | 2 (TOUCH2) | touch sensor |
| I2C de expansão | SDA 4 · SCL 5 | `i2c_master` |
| microSD externo | D0 6 · CLK 15 · CMD 16 | SDMMC 1-bit |

Cuidados:

- **GPIO47/48 são domínio 1.8 V** neste módulo: nunca recebem lógica 3.3 V.
- GPIO35–37 ligados à PSRAM octal e GPIO33/34 não expostos: indisponíveis.
- GPIO0 e GPIO45/46 são straps (46 nunca com sinal idle-HIGH); 43/44 console,
  19/20 USB nativo.
- Boards de referência do xiaozhi usam 47/48 para LED/botão: a board NB3 não
  herda esses pinos.

## Resultado da etapa 0 (2026-10-25)

xiaozhi v2.5.2 com a board NB3, ativado no xiaozhi.me.

- **Conversa em PT-BR:** entende bem e responde rápido. Gate de PT-BR aprovado.
- **Wake word:** "Jarvis" (a padrão em chinês não é pronunciável para nós).
- **Alcance do mic:** só funciona de perto. Os módulos INMP441 em mãos têm
  piso de ruído de cerca de −40 dBFS em silêncio, uns 40 dB acima do
  esperado para um INMP441 genuíno. Três módulos deram o mesmo nível em duas
  placas (Waveshare e Freenove), com firmware xiaozhi e com firmware mínimo,
  nos pinos 39, 13 e 14, com fios novos, alimentação direta, capacitor,
  amplificador desconectado e mic vedado. Causa: lote de mic ruim. Ganho
  digital não ajuda, porque sobe o ruído junto com a voz; fica em 0 dB.
- **Pendente:** trocar o mic por um de outra origem (ICS-43434 ou SPH0645) e
  medir de novo antes de mexer em ganho ou AEC.

## Riscos em aberto

- **Mic I2S de qualidade** — ver resultado da etapa 0; o alcance depende de
  um mic com piso de ruído normal.
- **Mic único sem AEC** — barge-in e wake word durante a fala do robô podem
  falhar; mitigação é half-duplex, e o CoreS3 é o upgrade se não bastar.
- **Privacidade** — áudio e conversas trafegam pelos servidores do xiaozhi.me;
  a memória local fica em claro no microSD.
- **Integridade do microSD** — queda de energia no meio de uma escrita;
  mitigação é escrita atômica, versão de formato e funcionar sem SD.
- **Dependência de terceiro** — disponibilidade e limites do plano gratuito.

## Documentos

- [ROADMAP.md](ROADMAP.md) — etapas 0–6, prova de sucesso e andamento de cada uma
- [VIDA.md](VIDA.md) — humor, estados comportamentais, personalidade e memória local

Planejados:


- `HARDWARE.md` — pinout detalhado, energia e cabeamento dos servos
- `SAFETY.md` — máquina de estados do `motion_safety` e limiares
