# Face V2 — expressões, overlays e gestos

Vocabulário visual da tela 320×240. Decisão: D-011. Os mocks animados estão
no canvas "Expressões NB3" (privado, no claude.ai); este arquivo é o que o
firmware segue.

## Princípios

- **Uma identidade:** olhos brancos sólidos sobre preto. Cápsula é o
  repouso; as outras formas saem dela.
- **Gesto e permanência:** entrada expressiva, estado mantido calmo, saída
  suave. Uma expressão mantida cai para ~30% do movimento.
- **Um adorno por vez** (mão, balão, corações, pontos, ?, z, !), só em picos,
  e some sozinho.
- **Silhueta primeiro, cor depois:** cor na tela só nas bochechas (salmão),
  na falha (âmbar) e no aviso de rede; o resto da cor fica nos LEDs.

## Formas dos olhos

| Forma | Como se desenha | Usada em |
|---|---|---|
| Cápsula | retângulo com raio total | neutro, atento (mais larga), contente (mais baixa) |
| Cápsulas desviadas | as duas sobem e vão para o canto juntas, sem inclinar | pensando |
| Tamanhos diferentes | um olho maior, rosto inclinado | curioso |
| Círculo | largura = altura | surpreso |
| Crescente ∩ | cápsula com um corte curvo por baixo | feliz (grosso), carinhoso (fino) |
| Meia-lua inclinada | cápsula com pálpebra de cima inclinada | triste (cai para fora), irritado (cai para dentro), envergonhado |
| Meia-lua + cápsula | pálpebra só em um olho | confuso |
| Crescente ∪ | crescente de cabeça para baixo | sonolento (etapa 3) |
| Espiral | traço girando | tonto (etapa 3) |

## Overlays

| Overlay | Quando | Movimento |
|---|---|---|
| Barras de áudio | escutando | 7 barras finas crescendo do centro, irregulares, seguindo o microfone |
| Pontinhos | pensando | três pontos acendendo em sequência |
| Mão no queixo | pensando há mais de ~1 s | a mão do desenho do usuário balança apoiando o queixo |
| Balãozinho | falando | surge no canto, três pontos pulam no ritmo da voz, some ~0,5 s após a fala |
| Corações | carinho | um coração sobe por pulso de carinho (4 s) |
| Bochechas | carinho, envergonhado | duas pílulas salmão pequenas e inclinadas no canto de baixo dos olhos |
| Brilhos | animado | estrelas de quatro pontas piscando |
| ? | confuso | pendula ±12° |
| Gota de suor | envergonhado | escorre uma vez |
| Lágrima | triste, na entrada | cai uma vez |
| Sem conexão | rede caiu | Wi-Fi riscado piscando e pontos |

As mãos são os desenhos do usuário (seis poses: pensando, confuso,
risadinha, sonolento, combinado, oi), recortados e usados como sprites
pequenos, animados por posição, giro e escala.

## O que entra agora e o que espera

Entram agora, porque os estados já existem: neutro, escuta, pensando,
falando, as emoções da conversa (feliz, animado, surpreso, curioso, confuso,
envergonhado, triste, irritado), tap, carinho, humor em repouso, falha e
aviso.

Esperam a etapa 3 (energia e reflexos): sonolento, dormindo, tonto,
risadinha, combinado e oi.

## Implementação

1. **Formas** no avatar: corte inferior curvo (crescente) como peça nova da
   pose, interpolável; poses das expressões refeitas com as formas acima.
2. **Camada de overlay** no display, um adorno por vez, com os sprites das
   mãos.
3. **Ligação aos estados:** escuta → barras, pensando → pontos e mão,
   fala → balão, carinho → corações e bochechas.
