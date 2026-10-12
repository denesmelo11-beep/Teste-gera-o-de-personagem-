# chargen — gerador procedural de personagens low-poly (C++20)

Gerador de personagens para um ARPG de fantasia sério com um pé no anime: população variada
(idades, personas heroicas/vilanescas/fofas/sexy..., cortes de cabelo), **corpos base sem roupa**
(só roupa íntima, como no Skyrim — equipamento vai por cima), anatomia muscular e óssea legível e
**parametrizável**, rosto 2D expressivo, cabelo e busto com física, e um visualizador
Raylib/OpenGL com resolução reduzida intencional.

O núcleo não tem dependências externas (só a biblioteca padrão de C++20; o encoder PNG, o
exportador glTF e um renderizador de preview em software foram escritos aqui). O visualizador usa
Raylib 5.5 (opcional).

| Folha de modelo (`chargen_viewer --studio`) | Anatomia (curvatura + vincos) |
|---|---|
| ![folha](docs/folha_modelo.png) | ![anatomia](docs/anatomia.png) |

| População natural + presets (`--population`) |
|---|
| ![populacao](docs/populacao.png) |

| Visão de jogo (480×270 ampliado) | Retratos |
|---|---|
| ![lowres](docs/elenco_lowres.png) | ![retratos](docs/retratos.png) |

| Os 18 cortes | Expressões |
|---|---|
| ![cortes](docs/cortes.png) | ![expressoes](docs/expressoes.png) |

![física](docs/fisica_corrida.png)
![esquiva](docs/fisica_esquiva.png)

## Compilar

```bash
# biblioteca, CLI e testes
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/chargen_tests

# com o visualizador Raylib (usa a raylib do sistema, uma árvore local, ou baixa a 5.5)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHARGEN_VIEWER=ON \
      [-DRAYLIB_SOURCE_DIR=/caminho/para/raylib]
cmake --build build
```

## Visualizador (Raylib / OpenGL 3.3)

```bash
chargen_viewer [--seed N] [--count K] [--res 480]      # interativo
chargen_viewer --shots pasta --seed 21                 # gera a bateria de imagens e sai
chargen_viewer --studio folha.png --seed 21            # folha de modelo: mulher e homem, 4 vistas
chargen_viewer --population pop.png --seed 40          # população natural + presets
# estilo da malha: padrão = suave com vincos; --smooth = totalmente suave; --flat = facetado
```

Teclas: `N` novos personagens · `1-5` idle/walk/run/attack/dodge · `R` correr em círculo ·
`E` expressões · `W` vento · `L` baixa resolução · `Espaço` pausa · arrastar = câmera.
Headless funciona com `xvfb-run -a chargen_viewer --shots out`.

## CLI (preview em software, export glTF)

```bash
chargen_cli sheet     --seed 12 --sex female --persona sexy   # turnaround
chargen_cli lineup    --seed 300 --count 8 [--age elder]
chargen_cli portraits --seed 200 --count 12
chargen_cli faces     --seed 301
chargen_cli anim      --seed 304 --clip run --frames 6        # inclui física secundária
chargen_cli export    --seed 42 --lod 0 --out heroi.glb
chargen_cli stats     --count 300
```

Filtros: `--sex female|male`, `--age young|adult|mature|elder`,
`--persona heroic|villainous|cute|sexy|stern|gentle|rogue|noble`,
`--archetype natural|bombshell|dorito|brute|amazon|slender|chibi|monster` (presets estilizados,
opcionais) e `--exaggeration 0..2`.

## Como funciona

```
Genome (DNA) ──► Builder ──► Character { Skeleton, RigInfo, PhysicsRig, Mesh, faceAtlas }
                                  │
         animation.hpp ◄──────────┤ clipes procedurais + skinning
         physics.hpp   ◄──────────┤ cadeias de mola (cabelo, cauda) + jiggle (busto)
         gltf.hpp      ◄──────────┤ .glb com skin, animações e atlas
         apps/viewer   ◄──────────┘ Raylib: shaders, resolução baixa, física em tempo real
```

### Genoma: população, personas e anatomia (`include/chargen/genome.hpp`)
- **População comum**: altura, gordura, músculo e ossatura são normais independentes; os extremos
  ficam nas caudas ou nos presets. Idade (jovem/adulto/maduro/idoso) mexe em altura, gordura,
  queda de tecidos, postura, linhas do rosto e grisalho; a persona puxa corpo, olhar, sobrancelha
  e boca de repouso.
- **Proporções** em unidades de cabeça (`h = altura / headsTall`), cânone estilizado de 6,4–6,6
  cabeças (rosto legível em baixa resolução), cintura de violão nas mulheres, V nos homens, coxas
  cheias e piso de espessura para braços.
- **`Anatomy`**: um bloco de campos 0..1 prontos para sliders — `definition` (quanto a gordura
  esconde as formas), grupos musculares (`neck, traps, deltoids, pecs, lats, abs, obliques,
  serratus, biceps, triceps, forearms, glutes, quads, adductors, hamstrings, calves`) e
  proeminência óssea (`clavicles, ribcage, scapulae, pelvis, knees, elbows, wrists`). O gerador
  amostra todos a partir dos eixos da população; um editor pode sobrescrever qualquer um.

### Corpo: uma superfície contínua
- **Tronco + pescoço** são um único *loft* de anéis superelípticos (largura/profundidade por curvas
  monotônicas), da linha do quadril até dentro do crânio. Os anéis ficam sobre os marcos
  anatômicos (sulcos do abdômen, borda do peitoral, clavículas, inclinação do trapézio).
- **Pelve e coxas** usam topologia de "calça": o anel inferior do tronco é uma sela (alto no
  quadril, baixo na virilha) e se divide em duas metades que, fechadas por uma ponte sob a
  virilha, viram o topo de cada coxa. Anéis de junção carregam o volume de quadril e glúteos.
  Não há superfícies se interpenetrando no quadril nem no pescoço.
- **Relevo anatômico** como campos aditivos: clavículas e fúrcula, peitorais com prateleira,
  arco costal, serrátil, reto abdominal com intersecções, oblíquos e o "V" inguinal, crista
  ilíaca, escápulas, eretores, trapézio, dorsais, covinhas lombares; nos braços deltoide (três
  cabeças fechando em V), bíceps, tríceps em ferradura, braquial, olécrano/epicôndilos,
  braquiorradial, punho achatado; nas pernas vasto lateral/medial, adutores, isquiotibiais,
  patela, gastrocnêmio (cabeça medial mais baixa), tíbia, Aquiles e maléolos. Mãos com sulcos de
  dedos, nós e polegar com eminência tenar.
- **Sombreamento**: normais suaves com detecção de vinco por ângulo (as dobras anatômicas ficam
  nítidas, as formas continuam lisas) e **cavidade baked na cor do vértice** a partir da curvatura
  local (sulcos levemente mais escuros e quentes, cristas mais claras) — o equivalente procedural
  de uma textura pintada à mão, independente da luz.
- **Roupa íntima** recortada por campos: triângulos são divididos exatamente na borda (com
  refinamento por bisseção), então as bordas são curvas limpas em qualquer ângulo.

### Rosto 2D e cabeça
Cabeça esculpida (mandíbula, queixo, maçãs, nariz, arcada) com mandíbula estilizada mais estreita
e crânio cheio. Olhos, sobrancelhas e boca são pintados com SDF num atlas 1024×512 (8 células de
olhos × 8 de boca, combináveis); olhos grandes e nítidos — linha de cílios forte com "asa", íris
alta com um único brilho, pálpebra inferior curta. Expressão = offset de UV. Barbas são pintadas
na pele com borda exata e ganham volume que afina até zero na borda.

### Cabelo e física
- Topo do crânio com volume (calota); cortes curtos esculpidos por campos de volume.
- Cortes médios/longos são **massas contínuas**: folhas grossas ao redor da nuca e das laterais,
  com mechas esculpidas como sulcos, pontas suaves na barra e duas mechas frontais caindo à frente
  dos ombros; franjas reta, de lado ou cortina no mesmo sistema. Rabos de cavalo, tranças e coques
  são tubos/volumes próprios.
- Cada massa é presa a uma fileira de cadeias de mola (estilo VRM spring bones) com colisores de
  cápsula (cabeça, pescoço, tronco, braços, coxas); o busto usa molas de jiggle limitadas a um
  elipsoide. Passo fixo de 120 Hz e inércia relativa ao corpo (cabelo não "voa" só porque o
  personagem anda).

### Esqueleto e animação
`root → hips → spine → chest → neck → head`, clavícula/braço/antebraço/mão (2 ou 4 braços),
coxa/canela/pé/dedo, ossos de busto, cauda, cadeias de cabelo e sockets de equipamento
(`socket.hand.R/L`, `socket.back`, `socket.hip`). Clipes procedurais: `idle`, `walk`, `run`,
`attack`, `dodge`, com trilha de expressão.

## Desempenho (Release)

| | triângulos | tempo/personagem |
|---|---|---|
| LOD0 | ~8.900 | ~29 ms (malha poucos ms; o resto é o atlas facial, em paralelo) |
| LOD1 | ~3.700 | ~25 ms |
| LOD2 | ~2.700 | ~24 ms |

## Estrutura

```
include/chargen/  math, rng, genome, character, animation, physics, image, render, gltf
src/              genome.cpp (população, personas, anatomia, paletas)
                  builder.*  (lofts, recortes, normais com vinco, cavidade, solda, pesos)
                  generator.cpp (tronco/pescoço, pelve-coxas, braços, mãos, pés, roupa íntima, colisores)
                  head.cpp (cabeça esculpida, decals) · hair.cpp (calota, massas, franjas, barba, orelhas)
                  face.cpp (atlas facial) · physics.cpp · animation.cpp · gltf.cpp · render.cpp
apps/             chargen_cli.cpp, viewer.cpp (Raylib)
tests/tests.cpp   docs/ (imagens geradas pelo visualizador)
```
