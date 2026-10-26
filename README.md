# NoiseBot v3

Companion robot de mesa: um corpo com personalidade própria (face, emoções,
movimento, toque, LEDs, reflexos) sobre uma mente de IA em nuvem. Liga na
tomada, conecta no WiFi e conversa — **nenhum serviço, server ou LLM rodando
fora do robô e da nuvem**.

> **Status:** etapas 0 e 1 concluídas; núcleos da etapa 2 (servos e safety)
> prontos. Ver [docs/ROADMAP.md](docs/ROADMAP.md).

## Princípios

1. **Sempre ligado, zero setup.** O robô funciona sozinho; nada roda fora dele
   e da nuvem.
2. **Commodity se compra, diferencial se constrói.** Voz, ASR, LLM, TTS,
   memória, Opus e protocolo vêm do xiaozhi. Face, personalidade, movimento e
   safety são nossos.
3. **Rigor onde machuca.** Safety de movimento, elétrica e núcleos de
   comportamento têm teste obrigatório. O resto é leve.
4. **Um dono por recurso.** Cada periférico tem uma única task que escreve
   nele; os demais enfileiram pedidos.
5. **Comportamento propõe, árbitro decide.** Modifiers sugerem expressão,
   olhar e movimento com prioridade; só o árbitro escreve no avatar e no motion.
6. **Upstream intacto.** Dependências externas entram por tag fixa + patch
   versionado — nunca fork.

## Arquitetura (planejada)

```
            nuvem (xiaozhi.me)
   ASR · LLM · TTS · memória · persona
                   │  WebSocket + Opus + MCP
┌──────────────────┴───────────────────────────┐
│ ESP32-S3 (Waveshare N32R16V)                 │
│                                              │
│  xiaozhi (tag + patch)  ──►  ponte NB3       │
│                               │              │
│  modifiers ──► árbitro ──► avatar / motion   │
│  (núcleos C puros)            │              │
│                         motion_safety (veto) │
│                               │              │
│  HAL: servos SCS, LEDs, toque, áudio, SD     │
└──────────────────────────────────────────────┘
```

Offline, o robô continua vivo: face, reflexos, relógio e timers rodam no
dispositivo.

## Hardware base

- **Waveshare ESP32-S3-DEV-KIT N32R16V** — ESP32-S3, 32 MB flash, 16 MB PSRAM.
- **Display ST7789 2"** (SPI).
- **Áudio I2S:** mic INMP441 + amplificador MAX98357 (mic único, sem AEC).
- **2× servo Feetech SCS0009** via FE-TTLinker (UART) — posição, carga,
  temperatura e tensão lidas do próprio servo.
- **LEDs WS2812** externos + LED onboard, **toque corporal** capacitivo,
  barramento **I2C** de expansão, **microSD** externo.
- Upgrade opcional futuro: **M5Stack CoreS3** (AEC com dois mics, touch,
  bateria) — não é pré-requisito de nenhuma etapa.

Pinout e cuidados elétricos: [docs/README.md](docs/README.md#hardware).

## Estrutura (planejada)

```
firmware/   firmware ESP-IDF (ver firmware/README.md)
docs/       decisões, hardware e roadmap (ver docs/README.md)
```

## Roadmap

Andamento detalhado de cada etapa, com o que foi entregue e o que falta:
[docs/ROADMAP.md](docs/ROADMAP.md).

| Etapa | Entrega | Prova de sucesso |
|---|---|---|
| 0 | xiaozhi v2.5.2 em **board customizada NB3** na Waveshare N32R16V | conversa em PT-BR agrada; wake word funciona |
| 1 | Firmware NB3 próprio (xiaozhi por tag + patch): face, emoções → expressões, toque, LED | o robô "vive" e conversa |
| 2 | Servos SCS + `motion_safety` + ferramentas MCP de movimento | olha e acena; safety corta com carga real |
| 3 | Agenda, timers e comportamentos offline como ferramentas do dispositivo | funciona sem nuvem para o que é local |
| 4 | Corpo/placa carrier com acústica tratada; CoreS3 se AEC fizer falta | só se necessário |

## Referências

- StackChan — https://github.com/m5stack/StackChan (estrutura de integração com xiaozhi)
- xiaozhi-esp32 — https://github.com/78/xiaozhi-esp32
