# Vida própria — humor, estados, personalidade e memória local

Especificação da camada que devolve ao NB3 a autonomia do Noise: o xiaozhi
continua dono da conversa rápida, e o ESP32 mantém humor contínuo,
temperamento, energia, estados comportamentais, reflexos e iniciativa. O
microSD guarda memória de médio e longo prazo, acessível pelo próprio
firmware e pela conversa via ferramentas MCP. Decisão: D-010.

Regra de divisão: **o SD armazena; o ESP32 executa a lógica.** Nenhum serviço
no PC é necessário para o robô funcionar.

## 1. Quem é dono do quê

| Camada | Dono | Observação |
|---|---|---|
| Sessão de conversa, áudio, ASR/LLM/TTS | xiaozhi + xiaozhi.me | intocado (tag + patch) |
| Estados operacionais | xiaozhi (`DeviceState`) | starting, wifi_configuring, idle, connecting, listening, speaking, notifying, upgrading, activating, audio_testing, fatal_error |
| Estados comportamentais | NB3 | camada acima, consome os operacionais como eventos |
| Humor, energia, personalidade | NB3 | núcleos C17 com host-test |
| Memória média e longa | NB3, persistida no microSD | lógica no ESP32, SD só armazena |
| Expressão, LEDs (e depois motion) | árbitro do NB3 | único escritor |

A camada comportamental **não duplica a máquina de conversa**: ela não decide
quando ouvir ou falar. Recebe as mudanças de `DeviceState` (hoje via
`Led::OnStateChanged()`) e as traduz em eventos próprios.

## 2. Estado, humor e energia são eixos separados

- **Estado comportamental** — o que o robô está fazendo: `IDLE`, `ENGAGED`
  (há conversa: listening/speaking/notifying), `REACTING` (toque, reflexo ou
  arco em curso), `SLEEPING`, `FAULT` (fatal_error do xiaozhi ou do
  `motion_safety`). Modo `quiet` é flag persistente, não estado.
- **Humor** — como ele se sente: vetor contínuo valência × ativação em
  [−1, +1]. Pode estar em IDLE e continuar contente ou irritado.
- **Energia** — disposição (sono, tédio): entra depois dos arcos (§8).

**Voltar ao IDLE limpa ações temporárias** (propostas transitórias do
árbitro, reação de toque, olhar e, no futuro, pescoço) e **preserva a
evolução do humor e da energia**. O humor só muda por estímulo e decaimento,
nunca por troca de estado.

## 3. Humor

Lógica numérica do `emotion_core` do NB2, sem o renderer antigo:

- estímulos somam deltas `{Δv, Δa}` com clamp por eixo;
- decaimento exponencial rumo ao **temperamento habitual** (no NB2,
  `(+0,10, +0,05)`: em paz, levemente caloroso e desperto), não ao zero;
- **recuperação assimétrica**: enquanto a valência é negativa, ela decai
  mais devagar (no NB2, constante 2× maior) — mágoa passa devagar;
- ativação recebe um offset circadiano quando a fonte de hora existir.

Fontes de estímulo: emoção recebida do xiaozhi (`SetEmotion`), toque,
eventos de conversa (wake, fala longa), lembranças relevantes da memória
média (§5) e a ferramenta MCP de estímulo (§6). O corpo **integra, não
obedece**: deltas externos têm teto e o decaimento continua valendo.

Saída: o vetor escolhe uma expressão do vocabulário de 14 (com histerese
para não alternar na fronteira) e a propõe ao árbitro como fonte `MOOD`, de
prioridade abaixo da emoção da conversa. O humor também modula intensidade e
ritmo dos LEDs. Pré-requisito: a duração da expressão no árbitro precisa
prevalecer sobre animações de uma execução (hoje HAPPY por 60 s volta ao
IDLE visual em ~2 s enquanto o árbitro ainda diz HAPPY).

## 4. Personalidade local

Parâmetros pequenos, com default no firmware e override no SD:

| Parâmetro | Efeito |
|---|---|
| Temperamento `(v₀, a₀)` | ponto de repouso do humor |
| Reatividade | ganho aplicado aos estímulos |
| Recuperação | constante de decaimento e fator de assimetria negativa |
| Iniciativa | frequência de comportamentos espontâneos no IDLE |

Padrões consolidados da memória longa (§5) podem deslocar esses parâmetros
devagar e com limites; isso entra depois que a memória média estiver
estável.

## 5. Memória

| Camada | Conteúdo | Meio | Sobrevive a reboot |
|---|---|---|---|
| Curta | humor, estado, últimos eventos (ring) | RAM | não |
| Média | acontecimentos relevantes que ainda influenciam o comportamento | microSD | sim |
| Longa | perfil, preferências, fatos, padrões consolidados | microSD | sim |

**Média:** cada episódio tem quando, tipo, valência associada, importância e
texto curto opcional. A importância **decai com o tempo** (meia-vida por
tipo); episódios abaixo de um limiar saem ou viram contagem num padrão da
memória longa. Episódios recentes e importantes viram estímulo leve de humor
(ex.: muito carinho hoje deixa o dia mais caloroso).

**Longa:** perfil (nome, apelido, idioma), preferências e fatos com origem,
data e contador de uso, e padrões consolidados (horários de interação,
frequência de carinho). Corrigir e esquecer são operações de primeira
classe: esquecer apaga do SD, não marca como oculto.

**Persistência:** núcleo C17 puro (importância, decaimento, consolidação,
busca por palavra/tag, limites de tamanho) + casca FATFS sobre SDMMC 1-bit
(GPIO 6/15/16). Arquivos com versão de formato, escrita atômica (temporário
+ renomear) e episódios em log de acréscimo. **Sem SD o robô funciona** só com
memória curta e avisa pelo banner.

## 6. Integração com a conversa (MCP)

Ferramentas do dispositivo, com descrições ricas para o LLM decidir por elas:

| Ferramenta | Faz |
|---|---|
| `self.mood.get` | estado, humor (e depois energia) e o motivo recente |
| `self.mood.stimulate` | a conversa sinaliza um acontecimento afetivo; delta limitado |
| `self.memory.recall` | busca lembranças por palavra, tipo ou período |
| `self.memory.remember` | registra fato, preferência ou acontecimento |
| `self.memory.correct` | corrige um registro existente |
| `self.memory.forget` | apaga um registro ou uma categoria |

A busca no ESP32 é simples (palavra/tag/período); a interpretação semântica
fica com o LLM.

## 7. Relação com a memória do xiaozhi.me

Não verificado ainda: se a memória do agente no xiaozhi.me está ligada,
o que ela guarda, por quanto tempo e como apagar. A memória local é a do
corpo e não depende disso; as duas podem coexistir. Verificar no console do
agente e registrar aqui.

## 8. Ordem e escopo

1. Humor + estados comportamentais + memória local mínima (episódios,
   perfil/fatos, MCP de consulta, registro, correção e esquecimento).
2. Arcos (birra e perdão, reconciliação, procura), energia, circadiano e
   iniciativa.
3. Movimento segue em entrega própria, sob `motion_safety`.

Portar os núcleos do NB2 integralmente não é requisito: recuperamos os
comportamentos desejados mantendo a simplicidade do NB3.
