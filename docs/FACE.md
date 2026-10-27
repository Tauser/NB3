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

Também na etapa 3, como **iniciativa no repouso**: de vez em quando, sem
interação há alguns minutos e em espera, o robô "pensa sozinho" por uns
5 s (olhar no canto, pontinhos e mão no queixo) e volta ao humor. A
frequência vem do parâmetro de iniciativa da personalidade; nunca durante
conversa, toque ou sono, e um toque ou a wake word interrompem na hora.

## Olhar autônomo

Em repouso o robô olha em volta sozinho, e o jeito de olhar mostra o humor
sem trocar a expressão. Vale no neutro e por cima de qualquer pose do humor
(contente, triste, carinhoso...). Nas reações (toque, escuta, pensando,
emoção da conversa, comando) o olhar volta ao centro e fica no usuário.

- **Sem ciclo:** cada olhar sorteia direção, distância, tempo de ida e tempo
  parado, dentro de limites. Às vezes volta ao centro (cerca de 1 em 4). A
  direção não repete nenhuma das duas últimas.
- **Ativação** muda o ritmo: alta olha mais rápido, mais longe e para
  menos tempo; baixa olha devagar, perto, demora em cada ponto e pisca
  devagar.

  | Ativação | Parado em cada ponto | Ida | Alcance | Piscar |
  |---|---|---|---|---|
  | −1 (calmo) | 3,5–7 s | ~0,7 s | 55% | 1,5× mais lento |
  | +1 (alerta) | 1,2–3 s | ~0,3 s | 100% | 0,8× (mais rápido) |

- **Valência** muda para onde: positiva puxa para cima e para os lados;
  negativa puxa para baixo e desvia (pouco centro, mais diagonais baixas).
- **Transição suave** entre olhar e não olhar (~0,4 s), para uma reação
  não dar um tranco ao começar nem ao acabar.
- **3D:** os olhos ficam sobre a frente de uma cabeça esférica que gira e é
  vista em perspectiva. Virando para um lado, os olhos atravessam a face, o
  do lado de lá fica menor e mais estreito e o do lado de cá maior; olhando
  para cima ou para baixo, eles achatam e se inclinam um para o outro, como
  as linhas de um rosto real. A face chega mais perto quando olha para baixo
  e as microssacadas continuam.

## Implementação

1. **Formas** no avatar: corte inferior curvo (crescente) como peça nova da
   pose, interpolável; poses das expressões refeitas com as formas acima.
2. **Camada de overlay** no display, um adorno por vez, com os sprites das
   mãos.
3. **Ligação aos estados:** escuta → barras, pensando → pontos e mão,
   fala → balão, carinho → corações e bochechas.
4. **Olhar autônomo:** `core/nb_gaze` (C17, semente injetada) sorteia os
   olhares a partir do humor; o avatar recebe o olhar pronto
   (`nb_avatar_core_set_look`) e o ritmo do piscar, e o display liga os dois
   ao humor e ao vencedor do árbitro.
