# Arquitetura do chargen

Mapa para quem vai mexer no código (pessoa ou IA). O README mostra **o que** o chargen faz;
este arquivo diz **onde** cada coisa mora, **como os dados fluem** e **quais regras não podem
quebrar**. Leia antes de pedir ou fazer uma mudança e atualize quando a estrutura mudar.

Para uma IA: a maior parte das tarefas se resolve lendo `include/chargen/*.hpp`,
`src/builder.hpp`, `src/generator_impl.hpp` e só o `.cpp` do módulo afetado. Não é preciso varrer
`docs/`, `tools/vitrine/` nem as imagens.

## Escopo e direção de arte

- **Dentro**: corpo, cabeça, rosto, cabelo, povos e traços de espécie, esqueleto, física
  secundária, animação procedural, morte e decomposição, utilidades de engine (chaves, hitboxes,
  sockets, LOD, famílias).
- **Fora**: equipamento e armaduras. Dependem demais do jogo que usa o motor. O corpo é uma base
  (roupa íntima só) e os sockets (`Socket`, `RigInfo::socket*`) são o ponto de encaixe, nada além.
- **Estilo**: low-poly facetado, charmoso, sem realismo. A identidade de um povo mora na
  silhueta (crânio, meio do rosto, mandíbula, altura dos olhos, orelhas), não em detalhe pintado.
  Olhos pequenos em órbitas sombreadas no jogo; olhos grandes e expressivos só em retratos.
- **Referências**: raças de D&D/WoW, fantasia sombria. Ben 10 só como lista de checagem de
  formas (`docs/especies.md`), nunca design copiado.

## Mapa de módulos

API pública em `include/chargen/` (namespace `cg`), implementação em `src/`
(internos em `cg::detail`). Uma biblioteca estática `chargen` (alias `chargen::chargen`).

| Header | Implementação | Responsabilidade |
|---|---|---|
| `math.hpp` | (header-only) | `Vec2/3/4`, `Quat`, `Mat4`, `lerp`, `smoothstep`... |
| `rng.hpp` | (header-only) | PCG32 determinístico, `fork(tag)` para sub-fluxos independentes |
| `genome.hpp` | `genome.cpp` | O "DNA": `Genome` (`BodyShape`, `FaceStyle`, `HeadForm`, `Palette`), `randomGenome`, `applySpecies`, `applyHead` |
| `character.hpp` | `generator.cpp` (+ os abaixo) | Saída: `Character` = `Mesh` + `Skeleton` + `RigInfo` + `PhysicsRig` + atlas facial. `generateCharacter` |
| (interno) `src/builder.hpp` | `builder.cpp` | Blocos de malha: `Part`, `Loft`, `SuperRing`, `Ellipsoid`, `ClipRegion`, pesos `SkinW`, `finalizeParts` (solda, normais, vincos, cavidade) |
| (interno) `src/generator_impl.hpp` | — | Classe `Builder`, `Marks` (pontos anatômicos), `LodSpec` |
| | `generator.cpp` | Marcas, esqueleto, tronco, braços, mãos, pernas, pés, cauda, espinhos, núcleo, pele (metal/carapaça), colisores, `Builder::build()` |
| | `head.cpp` | Cabeça esculpida, nariz, decals de olhos/boca, camada de pele do rosto |
| | `hair.cpp` | Cabelo (calota + mechas), barbas, chifres, orelhas, presas |
| | `traits.cpp` | Traços de espécie: asas, serpente/rastro/gosma, tentáculos, casco, lascas, mãos e pés especiais, bicos, mandíbulas, antenas, olhos compostos/ciclópicos, bocarra, olhos no corpo, corpo-planeta, quadrúpede, arte corporal, piercings |
| | `bones.cpp` | Esqueleto real dentro da carne, decomposição (`applyDecay`), buracos, constructos em ruína |
| | `face.cpp` | `paintFaceAtlas`: atlas 1024x512 com as expressões de olhos e boca |
| `physics.hpp` | `physics.cpp` | `SecondaryMotion`: cadeias de mola (cabelo, cauda, tentáculos, antenas) e jiggle do busto, 120 Hz |
| `animation.hpp` | `animation.cpp` | `Pose`, `Clip`, clipes procedurais (`make*Clip`), `limitPose`, `poseMatrices`, `skinMesh` |
| `motion.hpp` | `motion.cpp` | `Animator`: músculos (molas angulares), inércia, IK de pés/olhar/alcance, impactos, ragdoll Verlet |
| `engine.hpp` | `engine.cpp` | Chaves `CG1-`, `saveGenome/loadGenome`, métricas, hitboxes, sockets, silhuetas de cabeça, malha compacta, decomposição, LOD, populações, `childOf`, `aged`, `describe`, `validateCharacter`, `fuzzGenome` |
| `studies.hpp` | `studies.cpp` | Genomas feitos à mão e combinações de estresse (material de regressão) |
| `render.hpp` / `image.hpp` | `render.cpp` / `image.cpp` | Rasterizador em software (prévias, testes, CLI) e imagens RGBA/PNG |
| `gltf.hpp` | `gltf.cpp` | Export `.glb` (malha com pele, decals, esqueleto, clipes) |

Aplicações (só quando o chargen é o projeto principal, `CHARGEN_BUILD_TOOLS`):

- `apps/chargen_cli.cpp`: `lineup`, `export`, `faces`, `anim`, `atlas`, `sheet`, `portraits`,
  `stats`, `silhouettes`, `info`, `family`, `corpse`, `stress`, `motion`.
- `apps/viewer.cpp`: visualizador raylib (`-DCHARGEN_VIEWER=ON`), também gera as folhas e as
  cenas da vitrine (`--scene`, roteiros em `tools/vitrine/*.txt`, montagem em `montar.sh`).
- `tests/tests.cpp`: um único executável com `CHECK(...)`; falhas imprimem `FALHA arquivo:linha`.

## Fluxo de dados

```
seed + GenomeOptions ──randomGenome──▶ Genome ──generateCharacter(GenOptions)──▶ Character
   ▲        (genome.cpp)                 │            (Builder, generator.cpp …)        │
   │                                     │                                             │
CharacterKey "CG1-…"            saveGenome / loadGenome                  makeDefaultClips (animation.cpp)
(encodeKey/decodeKey)           (blob binário exato)                               │
                                                                                   ▼
                                                     Animator (motion.cpp) ◀── Clip[]
                                                        │  + SecondaryMotion (physics.cpp)
                                                        ▼
                                              Pose ──poseMatrices──▶ global / skin ──skinMesh──▶ vértices
                                                                       │
                                     hitVolumes / raycast / socketMatrix (engine.cpp)
```

1. **Genoma** (`randomGenome`): sorteia a pessoa em blocos, cada um com seu sub-fluxo
   `root.fork(n)` (ver tabela abaixo); depois `applySpecies` remodela para o povo e `applyHead`
   define o crânio (`HeadForm`) e o vocabulário de olhos (`EyeLook`).
2. **Construção** (`generateCharacter` → `Builder`): `applyDecay` ajusta o genoma do cadáver;
   `computeMarks` calcula os pontos anatômicos (`Marks`, em metros, com `h` = altura da cabeça);
   `Builder::build()` (fim de `generator.cpp`) chama, em ordem fixa, esqueleto → corpo (ou
   `buildBeastBody` em quadrúpedes) → cabeça → nariz → membros → traços → colisores → cabelo →
   ossos e decomposição. Cada `build*` empilha `Part`s; `finalizeParts` solda tudo numa `Mesh` com
   submeshes `Body`, `FaceEyes`, `FaceMouth`, `FaceSkin`. Por fim `paintFaceAtlas`.
3. **Rig**: os ossos são criados em `buildSkeleton` e outros `build*` (asas, cauda, cabelo...);
   `RigInfo` agrupa os ids por papel para o animador (braços, pernas, asas, serpente, tentáculos,
   antenas, pinças, sockets, limites de junta). Rotações de repouso são identidade.
4. **Animação**: `animation.cpp` gera clipes autorais lendo `RigInfo`; `bakePhysical` (em
   `makeDefaultClips`) (`motion.hpp`) passa pelo modelo muscular. Em tempo de jogo o `Animator` segue o clipe com
   molas, aplica IK, impactos e ragdoll, e chama `SecondaryMotion`.
5. **Engine**: tudo em `engine.cpp` lê o `Character` pronto; nada ali altera a malha.

## Invariantes (não quebrar)

### Determinismo e chaves

- Mesma seed + mesmas opções + mesma versão = mesmo personagem, bit a bit. Nada de
  `std::random_device`, hora, ordem de `unordered_map` ou dependência de threads no gerador.
- **Sub-fluxos fixos.** Cada bloco sorteia de `Rng(...).fork(tag)` para que acrescentar sorteios
  num bloco não embaralhe os outros. Tags em uso:

  | Tag | Onde | O quê |
  |---|---|---|
  | 1–8 | `randomGenome` | identidade, idade, persona, corpo, exagero, rosto, cabelo, cores |
  | 9 | `randomGenome` | povo sorteado em `mixedSpecies` |
  | 10 | `applySpecies` | ajustes do povo |
  | 11 | `applyHead` | `HeadForm` e olhos |
  | 23 | `randomGenome` | adornos: arte corporal, marcas de pelagem, tatuagens, piercings, bigodes |
  | 55, 57, 58 | `hair.cpp` | cabelo, chifres, barba |
  | 61 | `buildFurTufts` **e** `buildBeak` | reaproveitada nos dois (trocar muda personagens existentes) |
  | 73, 83 | `traits.cpp` | arte corporal (malha), olhos no corpo |
  | 77 | `fuzzGenome` | só fuzz |

  **Regra**: um sorteio novo vai para uma **tag nova** ou para o **fim** do bloco que já existe.
  Nunca insira um sorteio no meio de um bloco: todo personagem daquele bloco em diante muda.
- **Enums só crescem no fim.** `Species`, `HairStyle`, `Archetype`, `Persona` etc. são gravados
  como byte na chave `CG1-` e no blob. Novos valores entram antes de `Count`, nunca no meio. Os
  `static_assert` em `genome.cpp` obrigam as tabelas de nomes a acompanhar.
- **`mixedSpecies` só sorteia os povos clássicos** (Human..Skeleton, pesos em `root.fork(9)`).
  Povos de traço (`>= kFirstTraitSpecies`) ficam fora de propósito, para as chaves antigas
  continuarem reproduzindo. Não acrescente pesos a essa lista.
- O formato da chave (`encodeKey`: 20 bytes, checksum no byte 19) é estável. Campo novo em
  `GenomeOptions` exige pensar na chave; se mudar o layout, use um prefixo novo (`CG2-`) e mantenha
  `CG1-` decodificando.

### Layout do genoma salvo

- `Genome` precisa continuar **trivialmente copiável** (`static_assert` em `engine.cpp`):
  `saveGenome` grava os bytes crus. Nada de `std::string`, `std::vector` ou ponteiros nele.
- Campos novos vão **no fim** de `Genome` (como `HeadForm head`). `loadGenome` aceita o tamanho
  atual e o prefixo antigo (`offsetof(Genome, head)`); um campo novo no fim exige somar mais um
  tamanho aceito ali. Mudar um campo no meio invalida todos os saves.

### Malha e rig

- **Orçamento: 30 000 triângulos** (`validateCharacter`). Recortes caros se autolimitam (ex.:
  costura de magma/cristal só refina com `tris + 4*cut < 20000`). Corpos de quatro braços com asas
  são os que chegam perto: meça com `stress`.
- Até 4 influências por vértice, pesos somando 1 (`SkinW::normalize`); `Vertex::joints` e
  `PackedVertex::joints` são `uint8_t`, então **menos de 256 ossos**.
- Na pose de repouso, `poseMatrices` + `skinMesh` reproduzem a malha (erro < 1e-4, testado).
- Winding para fora; `validateCharacter` checa índices, NaN, normais unitárias, pesos, ids de
  ossos, faixas de submesh e limites plausíveis.
- Ordem de submeshes: `Body` primeiro, depois os decals. A cor de vértice carrega a classe de
  material no alfa (`(classe * 32 + 16) / 255`); `FaceSkin` é multiplicação 2x.
- Unidades: metros na saída; dentro do genoma, comprimentos do corpo em **unidades de cabeça**
  (`h = height / headsTall`). Coordenadas da cabeça: `x` através do rosto, `s` altura a partir do
  queixo.
- Todo clipe embutido respeita `RigInfo::limits` (`limitPose`).

### Cabeças e olhos

- Povo novo sem linha no `switch` de `applyHead` ganha o crânio humano liso.
- Em cabeças com focinho os olhos sobem por `eyeLift(g)` (`genome.hpp`) e ficam no crânio,
  nunca no focinho. Bicos crescem da raiz (`s = 0.25`), não de uma cunha de `rostrum`.
- Testes de silhueta: pares troll/morto-vivo/elfo noturno/alto elfo/orc/ogro precisam de IoU
  < 0.92 e (< 0.86 ou 0.02 melhor que o crânio humano), em `tests.cpp`. Meça com
  `chargen_cli silhouettes [--plain] [--all]`.

### Quadrúpedes (`stance = 1`)

- `torsoPoint` mapeia para o tronco horizontal (`th = pi/2` barriga, `3pi/2` coluna), então asas,
  espinhos e marcas acompanham. `legs[0,1]` dianteiras (filhas do peito), `legs[2,3]` traseiras.
  `trot` e `swipe` só existem para feras; outros corpos caem em `run` e `attack`.

## Receitas

### Adicionar um povo

1. `genome.hpp`: novo valor em `Species` **antes de `Count`** (depois de `Gravitant`).
2. `genome.cpp`: nome em `kSpeciesNames` (mesma posição); `case` em `applySpecies` (proporções,
   pele, traços, cabelo); linha em `applyHead` (crânio e `EyeLook`). Não mexa nos pesos de
   `mixedSpecies`.
3. Se precisar de forma nova, crie um **traço** em `BodyShape`/`FaceStyle` (campo no fim da
   struct, padrão "desligado") e o `build*` correspondente em `traits.cpp`, declarado em
   `Builder`. Traço novo serve a qualquer povo.
4. Se o traço tem ossos, exponha-os em `RigInfo` e trate em `animation.cpp`/`motion.cpp`.
5. Testes: o laço de povos de traço em `tests.cpp` já percorre todos; acrescente checagens
   específicas se o traço tiver regra própria. Rode `stress`.
6. Docs: linha no README (povos) e em `docs/especies.md` se cobrir uma forma da lista.

### Adicionar um estilo de cabelo

`HairStyle` antes de `Count` → `kHairNames` → `isShortHair`/`isTiedHair` se couber → construção
em `hair.cpp` (`buildHair`) → pesos por povo/persona em `genome.cpp` (no fim do bloco).

### Adicionar um clipe

`make*Clip` em `animation.cpp` lendo só `RigInfo` (nada de nomes de ossos fixos), eventos de
gameplay (`contact`, `release`, `footL/R`, `ragdoll`), curva de `tension`. Incluir em
`makeAuthoredClips`/`makeDefaultClips`. `checkMotion` em `tests.cpp` cobre poses finitas e pés.

## Verificação antes de abrir PR

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/chargen_cli stress --count 1000 --seed 1
```

É exatamente o que o CI (`.github/workflows/ci.yml`) roda, com gcc e clang. `stress` usa
`fuzzGenome` (todo botão fora da população natural) e falha se algum personagem não passar em
`validateCharacter` ou nas utilidades de engine; ele imprime também o pior tempo e o máximo de
triângulos. Para mudanças visuais, gere uma prévia (`chargen_cli lineup`, `portraits`, `sheet`)
e compare antes/depois: os testes garantem validade, não aparência.

## Lacunas conhecidas

- Nenhum teste fixa a saída de uma chave `CG1-` (hash da malha de seeds conhecidas), então a
  estabilidade das chaves é só convenção. Um teste "golden" pegaria sorteios inseridos no lugar
  errado.
- A tag 61 é compartilhada por tufos de pelo e bico (correlação entre os dois).
- Ainda não existem drider e pele vegetal. Centauro morto cai de barriga com as patas abertas.
