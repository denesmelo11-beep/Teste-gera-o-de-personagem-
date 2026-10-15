# Cobertura de formas de espécie

A variedade de corpos dos alienígenas de Ben 10 serviu só como **lista de checagem de formas**:
que estruturas de mão, membros, cabeças, peles e corpos um gerador precisa para cobrir quase
qualquer raça de fantasia. Nenhum design foi copiado. Cada linha abaixo diz qual forma aquele
alienígena exige e quais traços originais do chargen (genérico, sombrio, estilo D&D/WoW) a cobrem.

Legenda: ✓ coberto · ◐ aproximado · ✗ ainda não existe.

| Forma de referência | Exige | Traços do chargen | |
|---|---|---|---|
| Heatblast | corpo de rocha incandescente | `skinKind = 5` crosta de magma (Emberkin), olhos luminosos | ✓ |
| Diamondhead / Chromastone | pele de cristal, lascas | `skinKind = 4` cristal + `shards` (Crystalborn) | ✓ |
| Four Arms | quatro braços, gigante | `armPairs = 2`, cânone de ogro | ✓ |
| Stinkfly | asas de inseto, olhos de inseto | `wings = 3`, `eyeKind = 1`, `antennae` (Fae, Insectoid) | ✓ |
| Ripjaws | cabeça de peixe abissal, barbatanas, cauda | `EarStyle::Fin`, `crest = 1`, presas (Fishfolk), `lowerBody = 1` para a cauda | ◐ |
| Wildmutt / Benwolf | fera quadrúpede, focinho canino | Worgen (focinho, digitígrado, pelagem) | ◐ |
| Ghostfreak / Ectoturner | espectro sem pernas | `lowerBody = 2` rastro (Wraith) | ✓ |
| XLR8 | réptil veloz, cauda, viseira | Saurian: focinho réptil, cauda pesada, digitígrado | ◐ |
| Upgrade | corpo maleável metálico | `skinKind = 1` metal (constructos) | ◐ |
| Grey Matter / Brainstorm | criatura minúscula, crânio grande | Gnomo + crânio inchado (Cephalid); `pincers` para o caranguejo | ✓ |
| Cannonbolt | casco que enrola | `shell` (Tortle) | ◐ (sem rolar) |
| Wildvine | corpo vegetal, tentáculos | `hands = 3`, `backTentacles` | ◐ |
| Spitter / Arctiguana | réptil de focinho largo | `snout = 2`, escamas | ✓ |
| Way Big | gigante, crista na cabeça | cânone de ogro + `crest` | ✓ |
| Swampfire | carne vegetal com veios | ✗ pele vegetal (planejado) | ✗ |
| Humungousaur | dinossauro bípede, cauda, placas | Dragonborn/Saurian + `spines` + cauda maça | ✓ |
| Big Chill | asas de mariposa, sem pernas, gélido | `wings = 3` + `lowerBody = 2` | ✓ |
| Jetray | raia voadora, asas de membrana | `wings = 1`, `EarStyle::Fin`, cauda chicote | ◐ |
| Spidermonkey | quatro braços, várias pupilas | `armPairs = 2`, `eyePairs` | ✓ |
| Goop / Ditto | corpo amorfo, clones | ✗ fora de escopo de malha rígida | ✗ |
| Echo Echo | corpo pequeno e liso | gnomo + `skinKind = 1` | ◐ |
| Rath | felino humanoide | Worgen (focinho, garras, pelagem) | ◐ |
| Armodrillo | blindagem de placas | `skinKind = 2` carapaça + `spines` | ✓ |
| Kickin Hawk / Astrodactyl | homem-ave, bico, garras | Avian: `beak`, `wings = 2`, `feet = 1`, `hands = 1` | ✓ |
| Water Hazard / Crashhopper | exoesqueleto de inseto | Insectoid: carapaça, mandíbulas, antenas, quatro braços | ✓ |
| Squidstrictor | cabeça de lula | Cephalid: tentáculos faciais e nas costas | ✓ |
| Snare-oh | múmia | Mortos-vivos + bandagens (roupa) | ◐ |
| Frankenstrike / Blitzwolfer | monstros clássicos | Undead, Worgen, Ogro | ✓ |
| Atomix | gigante incandescente | `skinKind = 4/5`, sem orelhas | ◐ |
| Eye Guy | muitos olhos pelo corpo | `eyePairs` (só no rosto) | ◐ |
| Pesky Dust | fada pequena | Fae: pequena, asas de inseto, antenas plumosas | ◐ |
| Arachnichimp / Ball Weevil | aranha, aracnídeo | ✗ corpo inferior aracnídeo (drider) | ✗ |
| Way Bad / ciclopes | um olho central | ✗ olho ciclope | ✗ |

## Mapa por traço

- **Asas**: membrana, penas, inseto; abertas ou dobradas → dragões, demônios, aves, fadas, mariposas.
- **Corpo inferior**: serpente (naga, sereia de abismo, lâmia), rastro (espectros, gênios).
- **Mãos**: garra de três dedos, pinça, tentáculos → aves, crustáceos, horrores aquáticos.
- **Pés**: garras de ave.
- **Costas**: tentáculos, casco, lascas de cristal.
- **Cabeça**: três bicos, mandíbulas, antenas, olhos compostos, focinho réptil, quatro cristas,
  orelhas-barbatana ou nenhuma.
- **Pele**: escamas, cristal, magma, metal, carapaça.

Combinados com o que o motor já tinha (quatro braços, caudas, cristas dorsais, chifres, cascos,
digitígrado, centauro, mortos-vivos, esqueletos), isso cobre a maior parte das raças de D&D e WoW.
Lacunas mais úteis para a próxima rodada: corpo inferior aracnídeo (drider), olho ciclope e pele
vegetal.
