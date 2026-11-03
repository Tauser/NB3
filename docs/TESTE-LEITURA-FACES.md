# Teste de leitura das faces

Etapa 6 do `PLANO-FACE-E-COMPORTAMENTO-2026-11-02.md` (seção 10): as pessoas
leem a expressão no robô de verdade, sem legenda, e a taxa de acerto diz quais
faces se confundem.

## O que mede

As 13 faces que o árbitro resolve sozinho: neutro, feliz, animado, curioso,
confuso, bravo, surpreso, triste, sonolento, tonto, apaixonado, envergonhado e
contente. Cada avaliador vê **3 passagens** de todas elas em ordem aleatória
(39 itens, cerca de 5 minutos).

Fora do teste, porque dependem do estado do aparelho e não da expressão: as
barras de escuta, a mão de pensar e o balão de fala. Elas ficam para o roteiro
de conversa.

Critério (plano, seção 10): pelo menos 3 avaliadores; pares emocionais com 80%
de acerto agregado (neutro/contente, curioso/confuso, triste/sonolento/
envergonhado, feliz/apaixonado). Com menos de 3 avaliadores, o resultado vale
como indício e a limitação fica registrada. Amostra pequena, não é estudo.

## Como rodar

1. Gravar a versão de bancada na COM5 (confere o MAC antes):
   `idf.py -DNB3_FACE_QUIZ=1 build`, depois `-p COM5 -b 115200 flash`.
2. Deixar o robô em cima da mesa, parado, sem conversa. A mesa e a distância
   de uso normal (cerca de 60 cm), olhando a tela de frente.
3. Começa sozinho 20 s depois de ligar. Para cada item aparece o **número** na
   tela por 3 s, depois a face por 5 s. Nenhuma legenda.
4. Cada avaliador anota, a cada número, o nome da face que leu, **sem ver as
   respostas dos outros**. Uma linha por item, num CSV: `avaliador,item,resposta`.
   As respostas válidas são os 13 nomes acima, em minúsculas.
5. Guardar o log serial da rodada em um arquivo (a linha
   `KEY item N pass P face X` é o gabarito). Pode ser o monitor do
   `idf.py -p COM5 monitor` salvando a saída.
6. Todos os avaliadores veem a mesma rodada, cada um com a sua folha. A ordem
   é sorteada a cada boot: outra rodada tem outro gabarito e outro log.
7. Contar: `python tools/face_reading_score.py rodada.log respostas.csv`.
8. Voltar o firmware normal: `idf.py -DNB3_FACE_QUIZ=0 build` e gravar.

## Folha de respostas

```
avaliador,item,resposta
ana,1,feliz
ana,2,triste
```

Uma folha por avaliador. O contador junta as folhas passadas na linha de
comando.

## Resultado

Registrar em `docs/ROADMAP.md`: quantos avaliadores, taxa por face e por par,
as confusões mais comuns e o que mudar nos desenhos.
