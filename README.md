# FalseColor (OpenFX 1.4) para DaVinci Resolve — macOS

Plugin de false color por luminância de display (15 faixas), com entrada ACES via OpenColorIO,
float32, CPU de referência e Metal.

## Pré-requisitos (macOS)
- DaVinci Resolve 18 ou superior (OpenFX).
- Xcode com as Command Line Tools.
- CMake 3.20+, Git, Python 3.
- OpenColorIO 2.4.2+ (`brew install opencolorio`) — ou compilado do código-fonte.
- Cabeçalhos OpenFX: `git clone https://github.com/AcademySoftwareFoundation/openfx third_party/openfx`
  (ou `-DOFX_INCLUDE_DIR=<caminho>/include`).

## Compilação
A partir da pasta raiz (o caminho tem espaços; use aspas):

```bash
cd "$HOME/Desktop/FLMMKR FALSE COLOR"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```
**Se `test_ramp` ou `test_edges` falharem, não instale.** Testes pulados (código 77) aparecem como "Skipped".

O kernel Metal é compilado em tempo de execução a partir do código embutido (`scripts/embed_metal.py`),
então não precisa do Metal Toolchain offline. `palette_generated.h` é regenerado com
`cmake --build build --target regen_palette` (a paleta vive em `falsecolor_core.cpp`).

### Estrutura do bundle
```
FalseColor.ofx.bundle/
  Contents/
    Info.plist
    MacOS/FalseColor.ofx
```
`Win64/` e `Linux-x86-64/` só existem quando os builds Windows/Linux forem ativados.

## Instalação
```bash
scripts/install_macos.sh
```
Copia para `/Library/OFX/Plugins/` (pede senha), remove a quarentena e aplica assinatura ad-hoc.
Não copie pelo Finder (Gatekeeper). Feche e reabra o Resolve, vá à página Color, procure "OpenFX" na
Effects Library e arraste "FalseColor" para um nó.

## Uso
- **Display Rec.709 (pós-ODT):** nó depois do Output Transform; só faz clamp e false color.
- **ACES completo:** nó antes do Output Transform; Input Space = ACEScg / ACEScct / ACES2065-1,
  "OCIO Config" apontando para `configs/aces-1.x/config.ocio` ou `configs/aces-2.0/config.ocio`
  (veja os `README.txt` dessas pastas), e "ACES Version" igual à do projeto
  (Project Settings > Color Management).
- **Output View** é um campo de texto (listas dinâmicas não são portáveis entre hosts). Vazio = primeira
  view SDR Rec.709 da versão. As views disponíveis são escritas no log.
- **Render Path:** Automático (Metal se o host oferecer), CPU, GPU. Com ACES, a conversão OCIO roda na CPU
  e só o mapeamento de faixas roda na GPU (veja "Não implementado").
- **Log:** `~/Library/Logs/FalseColor.log` (backend, dispositivo, ms/frame na troca de backend; todo frame
  com `FALSECOLOR_VERBOSE=1` no ambiente do Resolve).

## Regra de faixa
`band = min(floor(Y*15 + 1e-6), 14)`. O `1e-6` evita que limites exatos (17/255 = 1/15) caiam na faixa errada por
arredondamento de float; é igual em CPU e kernels.

## Validação
Rampa de cinza 0–100% em display: as transições devem cair nos mesmos pontos da régua de referência.
Repita para cada versão de ACES. Com Render Path = GPU, confirme no log "render backend: Metal"; se aparecer
CPU ou "fallback", não está acelerado. Se o Resolve estiver em CPU, o plugin também fica em CPU.

## Problemas comuns
- Não aparece: nome do bundle, arquitetura, Resolve não reiniciado.
- Bloqueado pelo Gatekeeper: rodar `scripts/install_macos.sh` de novo.
- Resolve trava ao carregar: descritor/clipes; teste antes o exemplo "Invert" do OpenFX.
- Cores erradas: paleta 0–255 sem dividir por 255. Imagem preta/transparente: alpha não copiado.
- Lento: ACES 2.0 como gargalo (compare com Display Rec.709).
- Dylibs do OCIO: o plugin liga dinamicamente ao OCIO do Homebrew; para distribuir, linke OCIO estático ou
  embuta as dylibs no bundle.

## Distribuição
macOS exige assinatura Developer ID e notarização; sem isso o Gatekeeper bloqueia em outras máquinas.

## Windows (preparado, não testado)
Pasta de instalação: `C:\Program Files\Common Files\OFX\Plugins\`. O build exige Visual Studio 2022 com C++.
`scripts/install_windows.bat` e o bloco `if(WIN32)` do CMake **não foram validados**. Linux: fora do escopo.

## A verificar manualmente (nomes não confirmados)
- Propriedades OFX de GPU em `ofxGPURender.h`: `kOfxImageEffectPropMetalRenderSupported`, `kOfxImageEffectPropMetalEnabled`,
  `kOfxImageEffectPropMetalCommandQueue` (OpenCL/CUDA/OpenGL não são usadas). Conferir se `kOfxImagePropData` é o `id<MTLBuffer>`
  nesse modo no Resolve.
- Roles OCIO usados: `scene_linear` (ACEScg), `color_timing` (ACEScct), `aces_interchange` (ACES2065-1).
- Heurística de views por versão (`"2.0"`, `"1.0"`, `"SDR"`, `"709"` no nome) e de detecção de versão (nome/descrição do config).
- `BuiltinConfigRegistry` / URIs `ocio://...` (usados só nos testes, como alternativa quando não há config em `configs/`).
- Transform built-in do ACES 2.0 citado na especificação ("ACES-OUTPUT - ACES2065-1_to_CIE-XYZ-D65 - SDR-100nit-REC709_2.0"): não usado; confira se existe no seu config.
- `tests/reference/gray_ramp.png` e `falsecolor_bands.png`: não fornecidos.

## Não implementado nesta fase
- Geração de shader OCIO para MSL (`GpuShaderDesc`): a conversão ACES roda no CPU (buffer compartilhado) e o mapeamento de faixas na GPU.
- OpenCL e CUDA: kernels preparados (`kernels/falsecolor.cl`, `.cu`), fora do CMake e do descritor.
- Profundidades 8/16 bits (só float32 é anunciado). Imagens com `rowBytes` negativo não são tratadas.
