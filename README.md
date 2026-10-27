# FLMMKR FALSE COLOR (OpenFX) para DaVinci Resolve

False color por luminância em 15 faixas (Sistema de Zonas), com escala na tela, para macOS 13+
(Apple Silicon e Intel), Windows 10+ e Linux. Processamento na GPU (Metal no macOS), com a CPU como
rede de segurança. Não precisa de nenhum arquivo extra: todas as curvas estão embutidas no plugin.

## Parâmetros
| Parâmetro | O que faz |
|---|---|
| **Enable** | Desligado: a imagem passa sem alteração. |
| **Source** | Curva/espaço de cor da imagem que chega no nó: Rec.709, Rec.2020, Apple Gamma 2.0, ACEScct (AP1), DVR WG/Rec709, DVR WG/Intermediate, Cineon Film Log. |
| **Mix** | Mistura entre a imagem original (0) e o false color (1). |
| **Escala** | Mostra a escala do Sistema de Zonas no lado esquerdo da imagem. |
| **Legendas** | Mostra o significado de cada cor na escala (título, "TONS MÉDIOS" e o texto de cada faixa). |

### Como o Source é usado
1. O sinal é **decodificado com a curva do Source** (log ou gamma → luz linear).
2. Os primários do Source são convertidos para Rec.709 (matriz 3×3; Bradford para o branco do ACES).
3. Clamp em [0, 1].
4. O resultado é **recodificado como sinal de vídeo Rec.709**:
   - fontes de cena (log) — ACEScct, DVR WG/Intermediate, Cineon — usam a curva de câmera BT.709
     (OETF). Cinza 18% ≈ 41% → faixa verde (bloco 7);
   - fontes de display — Rec.2020, Apple Gamma 2.0, DVR WG/Rec709 — usam gamma 2.4;
   - Rec.709 passa direto (o valor do sinal já é a medida).
5. Y = luma Rec.709 desse sinal; `faixa = min(floor(Y × 15), 14)`; a régua vai de 0 a 109 IRE.

Para log, o plugin não aplica tone mapping (DaVinci DRT ou ACES Output Transform): mede a exposição
da cena numa curva Rec.709 padrão. As altas luzes, por isso, aparecem um pouco mais cedo no vermelho
do que na imagem final com roll-off.

### Sistema de Zonas
Os 15 blocos vão de baixo (preto) para cima (estourado). Os blocos 7 a 9 (verde, verde acinzentado e
rosa escuro — 44 a 65 IRE) formam a **zona de tons médios**, onde fica a pele humana. Na escala ela
aparece com contorno branco e o texto "TONS MÉDIOS".

## Onde colocar o nó
- Escolha no **Source** o espaço em que a imagem está **naquele ponto do grafo**:
  - timeline DaVinci Wide Gamut: "DVR WG/Intermediate";
  - ACES (ACEScct): "ACEScct (AP1)";
  - depois do Output Transform, ou numa timeline Rec.709: "Rec.709".
- O Resolve só passa GPU para o plugin quando o projeto está com processamento em GPU (Metal no Mac).
  O backend usado fica no log `~/Library/Logs/FalseColor.log`.

## Compilação
```bash
cd "$HOME/Desktop/FLMMKR FALSE COLOR"
git clone --depth 1 https://github.com/AcademySoftwareFoundation/openfx third_party/openfx
cmake -B build -DCMAKE_BUILD_TYPE=Release -DFALSECOLOR_UNIVERSAL=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
Não instale se `test_ramp`, `test_edges` ou `test_sources` falharem. Dependências: só os cabeçalhos
OpenFX, a biblioteca padrão C++ e o Metal (macOS). O OpenColorIO não é mais usado.

`build/fc_preview saida.ppm <source 0-6> <legendas 0|1>` renderiza uma rampa com a escala, para
conferir o visual sem abrir o Resolve.

### Arquitetura
- `fc_shared.h`: **única definição da matemática** (curvas, matriz, faixa, paleta, mistura da escala).
  É compilado como C++ na CPU e como Metal Shading Language no kernel (embutido por
  `scripts/embed_metal.py`). Os kernels OpenCL/CUDA preparados incluem o mesmo arquivo.
- `falsecolor_core.*`: paleta (fonte da verdade, gera `palette_generated.h`), nomes dos Sources,
  matrizes de primários, textos das faixas.
- `overlay.*` + `font5x7.h`: desenho da escala (fonte bitmap própria, sem fontes do sistema).
- `cpu_render.*`: renderizador de referência multithread. `metal_backend.*`: Metal.
- `FalseColorPlugin.cpp`: OFX (descritor, parâmetros, render, fallback GPU → CPU por frame).

## Instalação
Use os instaladores em `installer/releases` (gerados pelo CI) ou, no Mac, `scripts/install_macos.sh`.
Depois feche e reabra o Resolve. Ao atualizar de uma versão anterior, os nós antigos perdem os
parâmetros removidos (Input Space, ACES Version, OCIO Config, Output View, Luma Weights, Render Path).

## A verificar manualmente
- **Apple Gamma 2.0**: não encontrei definição pública. Está implementado como potência 2,0 com
  primários Rec.709.
- **DVR WG/Rec709**: implementado como primários DaVinci Wide Gamut com gamma 2.4.
- **Cineon Film Log**: curva Kodak (branco 685, preto 95, 0,002 densidade/código, gamma 0,6) com
  primários Rec.709.
- Propriedades OFX de GPU (`ofxGPURender.h`): `kOfxImageEffectPropMetalRenderSupported`,
  `kOfxImageEffectPropMetalEnabled`, `kOfxImageEffectPropMetalCommandQueue`.
- Referências usadas: ACEScct (ACES S-2016-001); DaVinci Intermediate e primários DWG (nota
  "Wide Gamut Intermediate" da Blackmagic, via biblioteca Colour Science); matriz AP1→Rec.709 publicada.

## Windows e Linux
Compilados e testados (testes unitários) pelo GitHub Actions; ainda não instalados numa máquina real.
Nesses sistemas o plugin roda na CPU (CUDA/OpenCL preparados, desativados). Windows: instala em
`C:\Program Files\Common Files\OFX\Plugins\`; Linux: `/usr/OFX/Plugins`.

## Distribuição
macOS exige assinatura Developer ID e notarização; sem isso o Gatekeeper pede clique direito > Abrir.
