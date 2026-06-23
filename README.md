# MidPoint Weka Benchmark

Este projeto organiza a implementação do classificador **MidPoint** para o Weka e um pipeline experimental para comparar o método com classificadores clássicos em bases ARFF.

O objetivo é avaliar uma heurística geométrica baseada em:

- protótipos originais;
- protótipos propagados entre vizinhos da mesma classe;
- barreiras geradas entre vizinhos de classes opostas;
- penalização de caminhos que atravessam regiões de conflito.

A implementação foi pensada para experimentos acadêmicos com foco em comparação contra KNN e outros classificadores de referência.

---

## Estrutura do projeto

Diretório principal esperado:

```bash
/home/breno/Downloads/AM/classificador
```

Estrutura usada:

```text
classificador/
├── bases/
│   ├── banana.arff
│   ├── banknote-authentication.arff
│   ├── blood-transfusion-service-center.arff
│   ├── climate-model-simulation-crashes.arff
│   ├── diabetes.arff
│   ├── ionosphere.arff
│   ├── phoneme.arff
│   ├── sonar.arff
│   ├── spambase.arff
│   └── wdbc.arff
│
├── midpoint-weka/
│   ├── src/weka/classifiers/misc/MidPoint.java
│   ├── build/
│   └── midpoint-weka.jar
│
├── results/
│   ├── results.csv
│   ├── summary_by_config.csv
│   ├── midpoint_vs_knn.csv
│   ├── classifier_specs.csv
│   ├── bench_latest.log
│   └── images/
│
├── bench
├── plot
└── README.md
```

---

## Dependências

O projeto usa:

- Java;
- Weka 3.8.7;
- Python 3;
- `matplotlib`;
- `numpy`;
- `pandas`.

Caminho esperado do Weka:

```bash
/home/breno/Downloads/weka-3-8-7-bellsoft-x64-linux/weka-3-8-7/weka.jar
```

Caminho esperado do JAR do MidPoint:

```bash
/home/breno/Downloads/AM/classificador/midpoint-weka/midpoint-weka.jar
```

---

## Compilar o MidPoint

Sempre que alterar `MidPoint.java`, recompile:

```bash
cd /home/breno/Downloads/AM/classificador/midpoint-weka

export WEKA_JAR="/home/breno/Downloads/weka-3-8-7-bellsoft-x64-linux/weka-3-8-7/weka.jar"

rm -rf build midpoint-weka.jar
mkdir -p build

javac -cp "$WEKA_JAR" \
  -d build \
  src/weka/classifiers/misc/MidPoint.java

jar cf midpoint-weka.jar -C build .
```

---

## Atalhos no terminal

Para chamar os scripts de qualquer pasta:

```bash
mkdir -p ~/.local/bin

ln -sf /home/breno/Downloads/AM/classificador/bench ~/.local/bin/bench
ln -sf /home/breno/Downloads/AM/classificador/plot  ~/.local/bin/plot

echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

---

## Executar o benchmark

O script `bench` executa os classificadores e salva os resultados em CSV.

Por padrão, ele roda:

- todas as bases;
- preprocessamento `both`, isto é, `raw` e `norm`;
- `10-fold cross-validation`;
- log automático;
- sem geração de imagens.

Rodar todas as bases com `seed=1` e `timeout=100`:

```bash
bench --seeds 1 --timeout 100
```

Rodar apenas uma base:

```bash
bench --bases sonar.arff --seeds 1 --timeout 100
```

Rodar bases específicas:

```bash
bench \
  --bases sonar.arff,ionosphere.arff,phoneme.arff,spambase.arff \
  --seeds 1 \
  --timeout 100
```

---

## Arquivos gerados pelo benchmark

Após executar o `bench`, os principais arquivos são:

```text
results/results.csv
results/summary_by_config.csv
results/midpoint_vs_knn.csv
results/classifier_specs.csv
results/bench_latest.log
```

### `results.csv`

Contém todas as execuções individuais, incluindo:

- base;
- algoritmo;
- variante;
- preprocessamento;
- seed;
- status;
- acurácia;
- precisão;
- recall;
- F1;
- MCC;
- Kappa;
- matriz de confusão;
- tempo;
- comando executado.

### `summary_by_config.csv`

Agrupa os resultados por configuração e calcula médias.

### `midpoint_vs_knn.csv`

Compara o melhor MidPoint contra o melhor KNN por base.

### `classifier_specs.csv`

Registra as configurações executadas por cada classificador.

---

## Gerar imagens

O script `plot` lê os CSVs já gerados pelo `bench` e cria figuras para artigo ou apresentação.

Rodar com configuração padrão:

```bash
plot
```

Rodar apenas para uma base:

```bash
plot --bases sonar.arff
```

Rodar para algumas bases:

```bash
plot --bases sonar.arff,ionosphere.arff,phoneme.arff,spambase.arff
```

Excluir bases suspeitas:

```bash
plot --exclude MagicTelescope.arff,eeg-eye-state.arff
```

Gerar também em PDF:

```bash
plot --pdf
```

As imagens são salvas em:

```bash
/home/breno/Downloads/AM/classificador/results/images
```

---

## Figuras geradas pelo `plot`

O script gera 10 imagens principais:

```text
01_executive_summary.png
02_accuracy_heatmap_best_by_algorithm.png
03_f1_kappa_heatmaps.png
04_midpoint_vs_knn_delta.png
05_midpoint_ablation_gain_heatmap.png
06_midpoint_configuration_matrix.png
07_confusion_error_comparison.png
08_accuracy_runtime_tradeoff.png
09_mean_rank_comparison.png
10_dataset_leaderboards.png
```

Essas figuras foram pensadas para:

- artigo científico;
- apresentação em slides;
- análise crítica do comportamento do MidPoint;
- comparação contra KNN e outros classificadores;
- análise de ablação;
- análise de trade-off entre desempenho e tempo.

---

## Classificadores comparados

O benchmark inclui:

- `ZeroR`;
- `MidPoint`;
- `KNN / IBk`;
- `RandomForest`;
- `SMO`;
- `Logistic`;
- `NaiveBayes`;
- `J48`;
- `LMT`;
- `AdaBoostM1`;
- `MultilayerPerceptron`.

O `ZeroR` é usado como baseline de classe majoritária.

---

## Preprocessamento

O parâmetro `--preprocess both` executa duas versões:

```text
raw  = base original
norm = base com Normalize do Weka
```

A normalização usada é:

```text
weka.filters.unsupervised.attribute.Normalize -S 1.0 -T 0.0
```

O `ZeroR` roda apenas uma vez, pois normalizar os atributos não altera o resultado dele.

---

## Parâmetros do MidPoint

Os principais parâmetros são:

```text
-S sameNeighbors
-D diffNeighbors
-R pathCandidates
-P barrierPenalty
```

Interpretação:

```text
-S  controla quantos vizinhos da mesma classe são usados para gerar protótipos propagados.
-D  controla quantos vizinhos de classe oposta são usados para gerar barreiras.
-R  controla quantos candidatos por classe são avaliados na classificação.
-P  controla o peso da penalidade causada pelas barreiras.
```

Exemplo de configuração que obteve bom resultado no `sonar.arff`:

```text
MidPoint F_R5_P05_norm
-S 1 -D 1 -R 5 -P 0.5
```

Resultado observado no `sonar.arff`:

```text
Acurácia: 89,4231%
F1 weighted: 0,894
Kappa: 0,7872
Matriz: Rock:85 12; Mine:10 101
```

---

## Relação com KNN

Sem protótipos propagados e sem barreiras, o MidPoint deve se comportar como um 1-NN:

```text
-S 0 -D 0 -R 1 -P 0.0
```

Essa configuração é chamada no benchmark de:

```text
NP_Control_R1
```

Ela funciona como ablação de controle. Assim, o ganho do MidPoint completo pode ser atribuído aos componentes adicionados:

- densidade por protótipos propagados;
- penalização por barreiras;
- combinação das duas heurísticas.

---

## Bases recomendadas

Após auditoria, o conjunto principal recomendado é:

```text
banana.arff
banknote-authentication.arff
blood-transfusion-service-center.arff
climate-model-simulation-crashes.arff
diabetes.arff
ionosphere.arff
phoneme.arff
sonar.arff
spambase.arff
wdbc.arff
```

Bases removidas do conjunto principal:

```text
MagicTelescope.arff
eeg-eye-state.arff
```

Motivos:

```text
MagicTelescope.arff possui vazamento por atributo ID artificial.
eeg-eye-state.arff possui dependência temporal incompatível com 10-fold CV aleatório.
```

---

## Comandos úteis

Rodar benchmark completo:

```bash
bench --seeds 1 --timeout 100
```

Rodar apenas Sonar:

```bash
bench --bases sonar.arff --seeds 1 --timeout 100
```

Gerar imagens após o benchmark:

```bash
plot
```

Gerar imagens somente do Sonar:

```bash
plot --bases sonar.arff
```

Gerar imagens e PDFs:

```bash
plot --pdf
```

Limpar imagens antigas:

```bash
rm -f /home/breno/Downloads/AM/classificador/results/images/*.png
rm -f /home/breno/Downloads/AM/classificador/results/images/*.pdf
```

Ver os melhores resultados rapidamente:

```bash
column -s, -t /home/breno/Downloads/AM/classificador/results/midpoint_vs_knn.csv | less -S
```

---

## Interpretação experimental

O MidPoint deve ser interpretado como uma extensão geométrica do 1-NN.

A hipótese principal é:

```text
A propagação de protótipos reforça regiões de suporte local da classe,
enquanto as barreiras penalizam decisões que atravessam regiões de conflito.
```

A leitura experimental recomendada é:

1. comparar MidPoint contra `NP_Control_R1`;
2. comparar MidPoint contra KNN com diferentes valores de `K`;
3. avaliar se o ganho vem de protótipos, barreiras ou da combinação;
4. usar F1, Kappa e MCC, não apenas acurácia;
5. analisar tempo de execução como trade-off computacional.

---

## Observações metodológicas

Para evitar conclusões frágeis:

- não usar bases com ID artificial;
- não usar bases temporais com validação cruzada aleatória;
- não interpretar acurácia isoladamente em bases desbalanceadas;
- sempre reportar matriz de confusão;
- manter o mesmo preprocessamento ao comparar MidPoint e KNN;
- usar múltiplas seeds quando o experimento final for consolidado.

---

## Licença e uso

Este projeto foi organizado para experimentação acadêmica com o classificador MidPoint no Weka.

Antes de usar os resultados em artigo, recomenda-se repetir os experimentos com múltiplas seeds e revisar as bases para evitar vazamento de dados, dependência temporal ou identificadores artificiais.
