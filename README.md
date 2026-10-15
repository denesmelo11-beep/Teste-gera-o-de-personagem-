# chargen — gerador procedural de personagens low-poly (C++20)

Gerador de personagens para um ARPG de fantasia sério com um pé no anime: população variada
(idades, personas heroicas/vilanescas/fofas/sexy..., cortes de cabelo), **corpos base sem roupa**
(só roupa íntima, como no Skyrim — equipamento vai por cima), anatomia muscular e óssea legível e
**parametrizável**, rosto 2D expressivo, cabelo e busto com física, e um visualizador
Raylib/OpenGL com resolução reduzida intencional.

O núcleo não tem dependências externas (só a biblioteca padrão de C++20; o encoder PNG, o
exportador glTF e um renderizador de preview em software foram escritos aqui). O visualizador usa
Raylib 5.5 (opcional).

| Folha de modelo (`chargen_viewer --studio`) | Anatomia em planos |
|---|---|
| ![folha](docs/folha_modelo.png) | ![anatomia](docs/anatomia.png) |

| População natural + presets (`--population`) |
|---|
| ![populacao](docs/populacao.png) |

| Visão de jogo (480×270 ampliado) | Retratos |
|---|---|
| ![lowres](docs/elenco_lowres.png) | ![retratos](docs/retratos.png) |

| Povos (`--races`): humano, anão, gnomo, orc, troll, morto-vivo, elfo noturno, alto-elfo, draenei, tauren, goblin, worgen, ogro, centauro, esqueleto |
|---|
| ![povos](docs/povos.png) |

| Worgen (pernas digitígradas, patas com garras, pelagem) | Morto-vivo (ossos expostos, carne manchada) |
|---|---|
| ![worgen](docs/folha_worgen.png) | ![morto-vivo](docs/folha_undead.png) |
| **Centauro** (torso humano sobre corpo equino, quatro pernas, trote) | **Ogro / colosso** (2,6–3 m, cabeça pequena, massa enorme) |
| ![centauro](docs/folha_centaur.png) | ![ogro](docs/folha_ogre.png) |

| Caudas, cristas dorsais e presas (`--anatomy`): réptil pesada, chicote com lâmina, maça com espinhos, rato; espinhos, placas e nós ao longo da coluna; presas abertas para fora vs. curvadas para dentro (mais imponentes) |
|---|
| ![caudas e presas](docs/caudas_presas.png) |

| Estudos e testes de estresse (`--studies`, `cg::studies()`): andróide monge com núcleo brilhante e pele metálica, homúnculo de sorriso de tubarão e sua forma com escudo de carapaça negra, gigantes de quatro braços e quatro olhos (3–3,5 m), centauro de quatro braços, horror de quitina, golem de ferro, gigante apodrecendo e sua ossada, andróide em ruína, gnomo no exagero máximo |
|---|
| ![estudos](docs/estudos.png) |
| ![estudos, rostos](docs/estudos_rostos.png) |

| Constructos não apodrecem (`skinKind` 1): desligam (olhos e núcleo apagam), levam amassados na carcaça, enferrujam em manchas foscas que se espalham, as placas rasgam com borda de metal raspado e por dentro aparece a armação de ferro; a partir de 0,5 um antebraço é arrancado (corte serrilhado, soquete escuro, ponta da armação e fios rompidos pendurados), e a perda continua na armação de ferro que sobra no fim |
|---|
| ![constructos](docs/constructos_ruina.png) |

| Morte e decomposição (`--decay`): vivo → recém-morto → apodrecendo → putrefato → quase ossada → ossada |
|---|
| ![decomposição](docs/decomposicao.png) |

| Quatro braços (`--arms 4`, qualquer povo) | Ataque com quatro braços |
|---|---|
| ![quatro braços](docs/folha_quatro_bracos.png) | ![ataque](docs/quatro_bracos_ataque.png) |

| Goblins: crânio em domo, queixo em ponta, nariz adunco, olhos miúdos em esclera amarelada, sorriso de presas |
|---|
| ![goblins](docs/goblins.png) |

| Gnomos: olhos afiados (não de boneca), nariz longo e fino, pés de galinha |
|---|
| ![gnomos](docs/gnomos.png) |

| Esqueleto (povo `skeleton`, olhos em brasa) | Esqueleto correndo (mesmo rig, todas as animações) |
|---|---|
| ![esqueleto](docs/folha_esqueleto.png) | ![corrida](docs/esqueleto_corrida.png) |

| Rostos dos povos (`--heads` com `CHARGEN_SPECIES=all`) |
|---|
| ![rostos dos povos](docs/povos_rostos.png) |

| Identidade: rostos aleatórios (homens em cima, mulheres embaixo) |
|---|
| ![rostos](docs/rostos.png) |

| Os 49 cortes (`--cuts`) | Expressões |
|---|---|
| ![cortes](docs/cortes.png) | ![expressoes](docs/expressoes.png) |

| Barbas (`--heads`): nenhuma, barba rala, bigode, cavanhaque, curta, cheia, longa, trançada |
|---|
| ![barbas](docs/cabelos_barbas.png) |

| Marcas (`CHARGEN_ART`, `CHARGEN_TATTOO`, `CHARGEN_PIERCE`, `CHARGEN_FURMARKS`): faixas tribais, pintura de guerra, manga de runas, cicatrizes, costelas pintadas, runas brilhantes; máscara pintada nos olhos, lágrimas, argolas e piercings; pelagem malhada, faixa no dorso, membros listrados |
|---|
| ![marcas](docs/marcas.png) |

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
chargen_viewer --heads cabecas.png --seed 60            # retratos masculinos: tour de barbas
chargen_viewer --cuts cortes.png --seed 21              # todos os cortes (CHARGEN_CUTYAW=2.4 mostra as costas)
chargen_viewer --faces rostos.png --seed 100            # rostos da população
chargen_viewer --races povos.png --seed 5               # cada povo, homem e mulher lado a lado
chargen_viewer --decay morte.png --seed 12              # estágios de decomposição até a ossada
chargen_viewer --decaylevel 0.7 ...                     # qualquer modo com os personagens mortos nesse estágio
chargen_viewer --anatomy caudas.png --seed 2            # tipos de cauda, cristas dorsais, presas para fora/dentro
chargen_viewer --studies estudos.png --seed 1           # estudos (CHARGEN_STUDYFACES=1 retratos; CHARGEN_STUDIES="a b" subconjunto)
chargen_viewer --peoples povos_tracos.png --seed 5      # os 20 povos de traços, homem e mulher (CHARGEN_FROM=cyclops: a partir de um povo)
chargen_viewer --traits tracos.png --seed 3             # 32 painéis, um traço de perto cada (CHARGEN_TRAITS="0 4 9" subconjunto)
# variáveis de ambiente úteis: CHARGEN_HEADRAND=m|f (retratos aleatórios por sexo), CHARGEN_HEADYAW,
# CHARGEN_HEADZOOM, CHARGEN_CUTS="19 20" (só alguns cortes, maiores), CHARGEN_NOSKIN=1 (sem camada de pele),
# CHARGEN_SPECIES=dwarf|...|all (retratos e folha de modelo de um povo)
# CHARGEN_TAIL=0..5, CHARGEN_SPINES="contagem tipo tamanho", CHARGEN_TUSKCURVE=-1..1, CHARGEN_HAIR=nome,
# CHARGEN_WINGS=0..3, CHARGEN_FOLD=1, CHARGEN_LOWER=0..3, CHARGEN_HANDS=0..4, CHARGEN_FEET=0..1, CHARGEN_BEAK=0..3,
# CHARGEN_CREST=0..4, CHARGEN_SKIN=0..5, CHARGEN_TENTACLES=0..6 (forçam traços de espécie em qualquer modo)
# CHARGEN_DEF / CHARGEN_MUSCLE=0..1 (forçam variáveis); CHARGEN_CHECK=1|2 conta (e lista) dobras e arestas abertas nos ombros
# estilo da malha: padrão = low-poly facetado; --crease = suave com vincos; --smooth = totalmente suave
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
chargen_cli info      --seed 9 --species troll --sex male     # chave, descrição, métricas, traços, hitboxes
chargen_cli info      --key CG1-1400000000001ZR1ZZZZY1000000000F
chargen_cli family    --seed 7 --species dwarf --children 3   # pais, filhos herdando, o pai envelhecendo
chargen_cli corpse    --days 30                               # linha do tempo da decomposição e regenerações
chargen_cli stress    --count 2000                            # fuzz: genomas extremos + validação + tempos
```

## Para a engine do RPG (`chargen/engine.hpp`)

Tudo o que o jogo precisa em volta do personagem gerado, sem depender do visualizador:

| Função | Para quê |
|---|---|
| `encodeKey` / `decodeKey` / `generateFromKey` | chave de texto compacta (`CG1-…`, 32 caracteres, com checksum) com seed + opções + decomposição + LOD: salva um NPC em poucos bytes e o regenera idêntico |
| `saveGenome` / `loadGenome` / `toBase32` | snapshot exato de um genoma editado (filhos, envelhecidos, sliders), com versão e checksum |
| `measure` | altura, altura dos olhos (câmera), ombros, quadril, alcance do braço, perna, massa estimada pelo volume do corpo, velocidade de andar/correr (número de Froude) e cápsula do controlador |
| `hitVolumes` / `raycast` / `overlapSphere` | cápsulas por osso ajustadas à malha, com zona (cabeça, pescoço, peito, abdômen, membros, mãos, pés, cauda), lado e multiplicador de dano; consultas de raio e esfera na pose animada |
| `socketMatrix` | matriz das âncoras de equipamento (mãos, costas, cinto, cabeça) na pose atual |
| `packMesh` | vértices de 32 bytes (contra 56) e índices de 16 bits: ~45% menos memória de GPU |
| `decayFromTime` / `decayStage` / `decayBucket` / `needsRebuild` | horas desde a morte → decomposição (clima ajustável), estágios (fresco, apodrecendo, putrefato, ossada) e quando regenerar o cadáver (só quando muda visivelmente; ~9 regenerações até a ossada) |
| `screenHeightPx` / `pickLod` | LOD pelo tamanho na tela, com histerese |
| `planPopulation` | chaves de uma vila inteira com pesos de povos, arquétipos, sexo, idade e quatro braços |
| `childOf` / `aged` | linhagens à Dwarf Fortress: filhos herdam pele, cabelo, cor e formato dos olhos, nariz, mandíbula, crânio, orelhas, altura e estrutura dos pais (com variação própria); envelhecer grisalha, enruga, cria bolsas e papada, curva, tira músculo e um pouco de altura |
| `physicalTraits` | força, agilidade, resistência, porte, presença e vigor (0..1) lidos do corpo, para semear atributos coerentes com a aparência |
| `describe` | uma linha para tooltips e logs: *"Troll man, 20, average build, 1.99 m; long tusks, spiked dorsal ridge, undercut hair"* |

Variáveis novas no genoma (`BodyShape`): `tailType` (0 simples, 1 réptil pesada, 2 chicote com lâmina, 3 peluda,
4 maça com espinhos, 5 de rato), `spines` / `spineKind` (espinhos, placas, nós) / `spineSize` (crista dorsal que
continua pela cauda) e `tuskCurve` (-1 abertas para fora … +1 curvadas para dentro); `skinKind` (0 carne,
1 metálica, 2 carapaça em placas facetadas com `carapace` = cobertura e `carapaceColor`), `core` (orbe
brilhante no abdômen ou no peito, `coreColor`, material emissivo `MaterialClass::Glow`). No rosto:
`eyePairs` (2 = segundo par de olhos, cada um numa órbita esculpida com pálpebras superior e inferior
modeladas) e `mouthStyle` 3 (sorriso de tubarão, duas fileiras de dentes).

Axilas: a junção braço–tronco é uma grade relaxada e reparada (colunas seguem o ângulo real em torno do
braço, cantos inferiores arredondados, oco da axila, braços pesados apoiados no flanco em vez de
afundar nele); `CHARGEN_CHECK=1` conta dobras: zero em 672 combinações de povo × 2/4 braços × seed e em
todos os presets.

`validateCharacter` confere índices, NaN, normais, pesos, ossos, faixas de submalha, limites e orçamento;
`fuzzGenome` sorteia genomas muito além da população (todos os povos, presets, quatro braços, caudas,
carapaças, núcleos, olhos extras, sliders extremos, decomposição e LOD). `chargen_cli stress --count 2000`
passa 100% (média ~50 ms por personagem com atlas; pior caso ~0,3 s num cadáver com ossos e cavidades).

Filtros: `--species human|dwarf|gnome|orc|troll|undead|nightelf|highelf|draenei|tauren|goblin|worgen|ogre|centaur|skeleton|mixed`, `--sex female|male`, `--age young|adult|mature|elder`,
`--persona heroic|villainous|cute|sexy|stern|gentle|rogue|noble`,
`--archetype natural|bombshell|dorito|brute|amazon|slender|chibi|monster` (presets estilizados,
opcionais), `--exaggeration 0..2` e `--decay 0..1` (morte e decomposição).

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
  ficam nas caudas ou nos presets. Um **tipo de corpo** (médio, magro, pesado, atarracado, longilíneo,
  forte) desloca esses eixos, então uma multidão mistura barrigudos, magrelos, troncudos e braçais. Idade (jovem/adulto/maduro/idoso) mexe em altura, gordura,
  queda de tecidos, postura, linhas do rosto e grisalho; a persona puxa corpo, olhar, sobrancelha
  e boca de repouso.
- **Proporções** em unidades de cabeça (`h = altura / headsTall`), cânone heroico-realista de ~7
  cabeças (corpos adultos, robustos, nada de magricelas; o rosto continua legível pelos planos), cintura de violão nas mulheres, V nos homens, coxas
  cheias, joelho alto (canela longa), panturrilha em losango com tornozelo fino e piso de espessura
  para braços.
- **`Anatomy`**: um bloco de campos 0..1 prontos para sliders — `definition` (quanto a gordura
  esconde as formas), grupos musculares (`neck, traps, deltoids, pecs, lats, abs, obliques,
  serratus, biceps, triceps, forearms, glutes, quads, adductors, hamstrings, calves`) e
  proeminência óssea (`clavicles, ribcage, scapulae, pelvis, knees, elbows, wrists`). O gerador
  amostra todos a partir dos eixos da população; um editor pode sobrescrever qualquer um.

### Rostos característicos dos povos
Além das proporções, cada povo tem um rosto próprio: `FaceStyle::headTop` (crânio em domo ou baixo),
`chinPoint` (queixo em ponta), `underbite` (mandíbula projetada), `wrinkles` (rugas no cenho, no
nariz, pés de galinha e sulcos), `sclera` (esclera amarelada) e `mouthStyle` (sorriso de presas
intercaladas, ou mordida com dentes inferiores). Goblin: domo, queixo agulha, nariz adunco,
olhos miúdos e oblíquos, sorriso de presas, rugas; orc: mandíbula projetada, olhos pequenos e
encapuzados, sobrancelhas pesadas; troll: rosto longo e estreito; ogro: crânio baixo; anão:
rugas e olhos fundos; elfos: queixo fino.

### Povos (`Species`)
Uma passada depois do sorteio remodela a pessoa no cânone do seu povo, mantendo a variação
individual (altura relativa, tipo de corpo, rosto): proporções (cabeças de altura, pernas, larguras
em unidades de cabeça, então um cânone baixo fica atarracado sozinho), corcunda real (o tronco e a
cabeça avançam), pele, orelhas (lâmina com comprimento, largura, ângulos e caída), presas curtas
(orc) ou longas e curvas (troll), focinho bovino (tauren), chifres (para trás, carneiro, eretos,
bovinos), cascos, cauda, olhos luminosos sem pupila (elfos, draenei, mortos-vivos), decomposição com
costuras (mortos-vivos), tentáculos faciais (draenei) e pesos próprios de cortes, cores e barbas.
- **Pernas digitígradas** (draenei, worgen): joelho alto e à frente, canela para trás até um
  jarrete elevado, metatarso longo até os dedos; termina em pata (almofada, quatro dedos com garras)
  ou casco fendido. O esqueleto usa os mesmos ossos (coxa, canela, pé, dedo), então as animações
  servem sem mudança.
- **Worgen**: focinho longo e estreito com parada abaixo dos olhos e trufa escura, orelhas no topo
  da cabeça, olhos âmbar luminosos, gola de pelo nas bochechas, mãos com dedos e garras, cauda
  peluda com física, pelagem (peito e garganta claros, dorso escuro, patas escuras) e tufos de pelo
  facetados nos braços, pernas, ombros e peito.
- **Mortos-vivos**: carne manchada (hematomas e putrefação por ruído 3D), costelas expostas no
  flanco, rádio e ulna ou tíbia à mostra (a carne é recortada exatamente por campos, com o osso
  dentro da cavidade) e mandíbula descarnada pintada na camada de pele.
- **Centauro**: o torso humano emerge do peito de um corpo equino (barril com peito alto, cilha
  funda, flanco recolhido, garupa redonda), patas dianteiras retas e traseiras com soldra à frente e
  jarrete atrás, boletos, quartelas e cascos, cauda de crina com física e sete pelagens (baio,
  alazão, preto, tordilho, palomino, lobuno, castanho). O esqueleto ganha duas pernas traseiras e as
  animações de andar/correr passam a trotar em pares diagonais.
- **Ogro (colosso)**: gigante de 2,6–3 m, cabeça pequena afundada nos trapézios, braços e mãos
  enormes, barriga, presas curtas.
- Paleta sombria: matizes fantásticos (verdes, azuis, lilases) são dessaturados e escurecidos.
`GenomeOptions::species` escolhe um povo; `mixedSpecies` sorteia a população do mundo inteiro.

### Traços de espécie (`src/traits.cpp`)
Peças anatômicas compostas livremente no genoma (`BodyShape` e `FaceStyle`), para chegar a quase
qualquer raça de fantasia sem um caso especial por povo. Cada uma tem ossos, pesos e limites no
mesmo esqueleto, então as animações e a física existentes as movem:
- **Asas** (`wings`): membrana de morcego/dragão, penas ou inseto; abertas ou dobradas nas costas;
  ossos `wing/wingarm/wingfore/winghand.L/R` em `rig.wings`.
- **Corpo inferior** (`lowerBody`): cauda de serpente enrolada (ossos `coil0..7` em `rig.serpent`,
  ponta com física), rastro espectral (`wisp0..3`) ou monte de gosma com poça (`ooze0..2`). Os ossos de perna continuam existindo, então
  clipes e retargeting não quebram.
- **Corpo de besta** (`stance = 1`): um quadrúpede de verdade, não um humano curvado. Tronco
  horizontal (garupa redonda, lombo recolhido, cernelha alta sobre um peito fundo), pescoço curto e
  grosso levando a cabeça baixa à frente, patas dianteiras maciças (escápula, úmero, antebraço,
  quartela) e traseiras digitígradas (joelho à frente, jarrete atrás), todas terminando em patas
  largas com almofada, quatro dedos e garras (ou cascos). As quatro patas são `rig.legs[0..3]`
  (dianteiras do peito, traseiras do quadril), então o trote em diagonal já funciona; os ossos de
  braço ficam recolhidos no peito para soquetes. Cauda, espinhos, tufos, asas, tentáculos, cristais,
  esqueleto e decomposição seguem o tronco deitado. `furPattern = 3` pinta listras de tigre.
  Funciona em qualquer povo (um worgen de quatro vira lobo atroz).
- **Corpo-planeta** (`orb`): o tronco engolido por uma esfera dos joelhos até acima da cabeça, o
  rosto saindo da frente, braços e pernas curtos, pedras orbitando.
- **Mãos** (`hands`): garras de três dedos, pinça de caranguejo (osso `pincer` com dobradiça,
  `rig.pincers`), tentáculos no lugar dos dedos, pás de escavar. **Pés** (`feet`): garras de ave.
- **Costas**: tentáculos com física (`backTentacles`, `rig.tentacles`), casco de tartaruga com
  plastrão (`shell`), lascas de cristal (`shards`).
- **Cabeça**: bico de rapina, longo ou curto (`beak`), mandíbulas, antenas finas ou plumosas com
  física (`rig.antennae`), olhos compostos, focinho réptil (`snout = 2`), orelhas-barbatana e sem
  orelha, cristas (barbatana, penas, gola óssea, capuz de naja), olho ciclope, rosto sem olhos
  (`eyeKind` 2 e 3) e bocarra de orelha a orelha com fileiras de dentes (`maw`).
- **Pele** (`skinKind`): escamas, cristal facetado com veios luminosos e crosta de magma com
  fissuras incandescentes, recortadas na malha ao longo das bordas das placas; olhos abertos pelo
  tronco e membros (`bodyEyes`), presos aos ossos da pele onde nascem.

Vinte povos usam esses traços: dragonborn, saurian (lagartos e kobolds), avian, naga, tortle,
insectoid, fishfolk, demon, cephalid, fae, crystalborn, emberkin, wraith, cyclops (gigantes de um
olho), talpid (toupeiras escavadoras), oculite (vigias cobertos de olhos), slime (gosma viva),
rimemaw (caçadores do gelo de bocarra), gloomhound (caçadores cegos de quatro patas) e gravitant
(mundos vivos). Eles ficam fora de
`mixedSpecies` (as chaves antigas geram os mesmos personagens); peça-os por `GenomeOptions::species`
ou ligue qualquer traço em qualquer povo. Hitboxes novas: `wing` e `tentacle`.
A cobertura de formas usada como referência está em [docs/especies.md](docs/especies.md).

![Povos de traços](docs/povos_tracos.png)
![Traços de perto](docs/tracos.png)

### Tom
Sério e sombrio: fundo de estúdio ardósia-azulado, sombras profundas e frias, gradação que tira a
saturação ("sem cores de bala"), peles castigadas em vez de porcelana, tons fantásticos apagados,
cabelos tingidos desbotados.

### Morte, decomposição e esqueletos
No jogo os personagens apodrecem e viram ossada ao morrer. `GenOptions::decay` (0..1, ou
`--decay` na CLI) gera o cadáver em qualquer estágio; o jogo só regenera o personagem conforme o
tempo passa (mesma semente, mesmo rig, mesmas animações):
- **0,15 recém-morto**: palidez acinzentada, olhos leitosos, pálpebras caídas.
- **0,4 apodrecendo**: carne manchada (hematomas, putrefação esverdeada), definhamento sobre os
  ossos, os primeiros rasgos pequenos já mostrando o esqueleto real, cabelo desbotado.
- **0,7 putrefato**: couro escuro, órbitas vazias, e a carne se rasga em buracos recortados
  exatamente (campos de recorte) mostrando o **esqueleto real** dentro: costelas, coluna, fêmur,
  úmero, rádio e ulna; atrás dos ossos, a parede interna escura da cavidade.
- **1 ossada**: só o esqueleto. Ele é construído a partir dos marcos do próprio corpo (crânio
  com órbitas e abertura nasal recortadas, dentes, mandíbula, coluna com processos espinhosos,
  caixa torácica com esterno e costelas flutuantes, clavículas, escápulas, pelve com ílios e
  sacro, ossos longos com epífises, mãos e pés com dedos), então cada povo vira a sua ossada:
  crânio de lobo no worgen, pernas digitígradas e cascos, o esqueleto equino do centauro, chifres e
  presas que ficam, cauda em vértebras.
- **Povo `skeleton`**: esqueletos que se levantam, com brasas frias nas órbitas.
- **Ossos proporcionais**: `BodyShape::boneMass` (ossatura e musculatura do indivíduo, vezes o
  povo: anões, orcs, tauren e ogros robustos; elfos, gnomos e goblins delicados) define a espessura
  das diáfises e das articulações. As articulações são peças próprias: cabeça do úmero, côndilos e
  olécrano no cotovelo, cabeça do fêmur e trocânter, côndilos, patela e platô tibial no joelho,
  maléolos no tornozelo; as diáfises param antes delas, então cada osso se lê separado.
- **Extremidades primeiro**: a partir de ~0,85 as mãos e os pés já são só ossos.
- **Bordas rasgadas**: os buracos na carne têm contorno irregular e uma borda de carne escura.
- **Mortos-vivos** (povo `undead`) mostram o esqueleto real por rasgos pequenos na carne.

### Amplitude de movimento
`RigInfo::limits` + `cg::limitPose`: cotovelos e joelhos são dobradiças que só dobram para um
lado (0 a ~150°), ombros, quadris, coluna, pescoço, punhos e tornozelos são cones com limites
anatômicos. Todas as animações embutidas respeitam os limites, e o jogo pode chamar `limitPose`
sobre poses próprias (IK, ragdoll).

### Quatro braços
`GenomeOptions::armPairs = 2` (CLI `--arms 4`) dá a qualquer povo um segundo par de braços,
nascido de uma segunda cintura escapular nas costelas inferiores e costurado na superfície do
tronco como o primeiro; o esqueleto ganha clavículas e úmeros extras e as animações movem os dois
pares (os de baixo em contraponto).

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
- **Estilo**: por padrão tudo é **facetado** (corpo, cabeça e cabelo), com anéis posicionados nos
  marcos anatômicos, então cada músculo/osso vira um plano legível com poucos polígonos. Os decals
  do rosto são projetados na cabeça low-poly real. Opcionalmente: normais suaves com vinco por
  ângulo (`--crease`) ou totalmente suaves (`--smooth`). Uma leve cavidade baked na cor do vértice
  reforça sulcos sem desenhar linhas.
- **Roupa íntima** recortada por campos: triângulos são divididos exatamente na borda (com
  refinamento por bisseção), então as bordas são curvas limpas em qualquer ângulo.

### Rosto e cabeça: identidade
- **Crânio em planos**: anéis da cabeça sobre os marcos (queixo, mandíbula, boca, base do nariz,
  maçãs, linha dos olhos, arcada, testa, crânio) e um eixo central de vértices como na referência.
  Variam de forma ampla: ângulo da mandíbula, largura do queixo (pontudo a quadrado), maçãs e o
  vazio abaixo delas, papada, largura do rosto, arcada e profundidade dos olhos, crânio.
- **Nariz** é uma peça facetada própria (crista, laterais, ponta, asas e base) com comprimento,
  projeção, largura, giba e ângulo da ponta: é o maior quebrador de planos do rosto.
- **Olhos** no estilo da referência: abertura larga (cerca de metade da largura em altura), íris
  grande e redonda cortada pelas duas pálpebras, cílio superior grosso com asa, vinco da pálpebra;
  sete famílias (padrão, redondo, estreito, afiado, caído, encapuzado, pequeno) com tamanho,
  espaçamento e inclinação próprios. Sobrancelhas, boca e expressões são SDF num atlas 1024×512.
- **Camada de pele**: uma cópia da frente da cabeça facetada com UVs para uma célula do atlas,
  desenhada como multiplicação 2× (o sombreamento dos planos sobrevive). Nela: barba rala,
  órbitas fundas, olheiras, maçãs encovadas, pele castigada (nariz e bochechas vermelhos), sujeira,
  sardas, rugas da testa e do cenho, cicatrizes (sobrancelha-bochecha, nariz, lábio, talho, olho
  cego com a íris leitosa) e tatuagens (linhas rúnicas, garras na bochecha, sigilo na testa).
- **Barbas** crescem como o cabelo: colunas de mechas enraizadas na pele ao longo da linha da
  barba, penteadas para baixo pelo rosto e, depois da mandíbula, caindo com gravidade (presas fora do
  pescoço e do peito). A borda superior fica rente à pele e continua na sombra pintada. Tipos:
  barba rala, bigode, cavanhaque, curta, cheia, longa, trançada (tranças com física) e costeletas.

### Cabelo e física
- Catálogo pensado para um mundo medieval sombrio: rente, raspado, repartido, para trás, bagunçado,
  tonsura de monge, corte tigela, tranças de guerreiro, trança-coroa, tranças, coques, rabos de
  cavalo, dreads, cachos longos, muito longo e outros (49 no total, com pesos por sexo, idade, persona e povo).
- Cortes de guerreiro e de povo: crista penteada para trás sobre laterais raspadas (moicano de orc e
  troll, terminando num rabicho), crista trançada nórdica, rabo alto com laterais raspadas, topete,
  trancinhas rente ao couro (cornrows), meia cabeça raspada, juba selvagem, franja longa sobre um
  olho, tranças finas com contas de metal, maria-chiquinha, coques duplos e mechas em tufos.
- Topo do crânio com volume (calota); cortes curtos esculpidos por campos de volume.
- Cortes curtos, topo dos longos e presos: **mechas facetadas** (cunhas com crista e ponta) deitadas
  sobre uma calota fina, sobrepostas como telhas e penteadas por um campo de direção próprio de
  cada corte (repartido, para trás, bagunçado, em direção ao elástico do rabo de cavalo...).
- Cortes médios/longos são **massas contínuas**: folhas grossas enraizadas sob a coroa, ao redor da
  nuca e das laterais, com mechas em cunha (crista/sulco facetados, comprimentos individuais, sem
  linhas escuras divisórias), pontas na barra e duas mechas frontais caindo à frente dos ombros; franjas reta, de lado ou cortina no mesmo sistema. Rabos de cavalo, tranças e coques
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
| LOD0 | ~5.900 | ~39 ms (malha poucos ms; o resto é o atlas facial, em paralelo) |
| LOD1 | ~3.400 | ~32 ms |
| LOD2 | ~2.800 | ~32 ms |

A camada de pele reaproveita ~400 triângulos da frente da cabeça (um draw a mais, com blend).

Cadáveres: enquanto ainda há carne, só ficam os ossos que algum rasgo deixa ver (e as mãos e pés
já descarnados); as bordas dos rasgos quase não refinam a malha. Médias (30 personagens):

| decomposição | LOD0 | LOD1 | LOD2 |
|---|---|---|---|
| 0 (vivo) | ~5.800 | ~3.400 | ~2.800 |
| 0,3–0,5 | ~8.100 | ~4.800 | ~4.100 |
| 0,7–0,96 | ~10.600–11.100 | ~6.200–6.500 | ~5.500–6.000 |
| 1 (ossada) | ~5.100 | ~4.350 | ~4.350 |

No estilo facetado cada face tem vértices próprios (≈3× vértices por triângulo); se memória de
vértices importar mais que a contagem de triângulos, gere com `--smooth` e faceteie no shader
(`normalize(cross(dFdx(pos), dFdy(pos)))`, já disponível no visualizador).

## Estrutura

```
include/chargen/  math, rng, genome, character, animation, physics, image, render, gltf, engine, studies
src/              genome.cpp (população, personas, anatomia, paletas)
                  builder.*  (lofts, recortes, normais com vinco, cavidade, solda, pesos)
                  generator.cpp (tronco/pescoço, pelve-coxas, braços, mãos, pés, roupa íntima, caudas, cristas dorsais, colisores)
                  head.cpp (cabeça, nariz, decals, camada de pele) · hair.cpp (calota, massas, franjas, barbas, orelhas)
                  traits.cpp (asas, serpente, pinças, tentáculos, casco, bicos, antenas, cristas)
                  bones.cpp (esqueleto real, decomposição, buracos na carne, esqueletos)
                  face.cpp (atlas facial) · physics.cpp · animation.cpp · gltf.cpp · render.cpp
                  engine.cpp (chaves, métricas, hitboxes, sockets, malha compacta, decomposição, LOD, famílias, validação, fuzz)
                  studies.cpp (estudos de personagem e combinações de estresse)
apps/             chargen_cli.cpp, viewer.cpp (Raylib)
tests/tests.cpp   docs/ (imagens geradas pelo visualizador)
```
