# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Visão geral

Instalação interativa de projeção: a webcam (Logitech C920) captura pessoas e um app OpenFrameworks (C++/GLSL, `of_void_cloud/`) projeta efeitos dentro e em volta das silhuetas, ancorados na pose de cada pessoa. É um único processo, sem Python. Código, comentários e identificadores estão em português; siga esse padrão. Não há testes, linter nem git.

## Comandos

- Rodar: `./executar_void_gpu.sh` (compila se preciso e abre). `VISUAL_FRAMES_DEBUG=1 ./executar_void_gpu.sh` já inicia no modo debug. `VISUAL_FRAMES_EFEITO=bichinho|ecos|paraquedas|lego` abre direto nesse efeito. `VISUAL_FRAMES_MONITOR=1 ./executar_void_gpu.sh` abre em tela cheia no monitor 1 (projetor no HDMI, com as telas no modo estendido) e esconde o cursor.
- Compilar: `make -C of_void_cloud -j$(nproc)` → `of_void_cloud/bin/of_void_cloud`. O executável precisa rodar de dentro de `of_void_cloud/bin/`, porque os caminhos dos modelos são relativos a ele.
- Benchmark do detector, isolado do OF: `of_void_cloud/ferramentas/compilar_benchmark.sh` e depois `of_void_cloud/bin/benchmark <seg.onnx> <pose.onnx> [imagem] [saida_debug.jpg]`. Sem imagem, usa um quadro da câmera.
- O app abre duas janelas: **Projeção** (vai para o HDMI) e **Painel de controle** (fica no notebook; escolhe o efeito e os ajustes).
- Teclas: `1` Luz, `2` Bichinho, `3` Ecos em quadrinhos, `4` Salto de paraquedas, `5` LEGO, `d` debug (câmera, caixas, IDs, esqueleto, tempos), `f` tela cheia, `q`/Esc sai.

## Arquitetura (`of_void_cloud/src/`)

Fluxo: thread de visão (`ProcessadorVisao`) → `ResultadoVisao` publicado sob mutex → `ofApp` copia o mais novo em `update()` e renderiza a 60 fps.

- `Configuracao.h`: todos os parâmetros ajustáveis (câmera, modelos, limiares, One Euro, filtro guiado), com a origem dos valores comentada. Comece por aqui para tunar.
- `DetectorPessoas`: ONNX Runtime CUDA. Roda dois modelos YOLO26 em paralelo (`std::async`) sobre o mesmo tensor: **seg** (`yolo26n-seg`) e **pose** (`yolo26s-pose`). Detecções de pose e de máscara são casadas por IoU de caixa. Não depende do OF.
  - Modelos exportados com `imgsz=(384,640)`, `nms=False`, opset 17, FP32. A entrada é letterbox de 1280x720 → 640x360 + 12 px de borda vertical. Saídas end-to-end: `[x1,y1,x2,y2,conf,cls,…extras]` ordenadas por conf. Os extras são 17×(x,y,vis) na pose e 32 coeficientes na seg, mais o protótipo `(32,96,160)`. A seg usa as 80 classes do COCO, então filtra `cls==0`.
  - A máscara (coeficientes·protótipo) é calculada e ampliada **só na região da caixa** (`warpAffine` com a convenção de meio pixel do `cv::resize`), em coordenadas 640x360.
  - Para re-exportar é preciso o Ultralytics, num venv descartável fora do repo. Ao trocar `imgsz`, ajuste `ENTRADA_*` no config.
- `RastreadorPessoas`: estilo ByteTrack sem Kalman: associação em 2 etapas (alta/baixa confiança), memória de 30 quadros, slot estável 0..5 por pessoa (usado para cor no shader), supressão de duplicatas. Keypoints suavizados com `FiltroUmEuro` em pixels. A velocidade dos pulsos é medida em alturas-de-corpo/s.
- `ProcessadorVisao`: câmera V4L2/MJPG → detector → rastreador → composição da máscara. Cada pixel fica com a pessoa de maior logit. Depois vêm sigmoid, filtro guiado (`ximgproc`, guiado pela câmera em cinza) e mistura temporal. O resultado sai como `CV_8UC2` = [alpha, slot+1].
- `AnalisadorAudio`: thread que captura o que toca no PC (`@DEFAULT_MONITOR@` via `libpulse-simple`, funciona no PipeWire), faz FFT (kissfft do OF), gera níveis grave/médio/agudo com ganho automático e detecta batidas no grave (método de Patin: energia > C × média de ~1 s).
- `ofApp`: sobe a máscara como textura `GL_RG8` e passa ao fragment shader (inline em `ofApp.cpp`) os keypoints (`pontos[6*17]` em uv + frescor), `pessoas[6]` (presença, energia das mãos E/D, altura do corpo), a música (`musica` = grave/médio/agudo/flash, `matiz`) e, por pessoa, cabeça suavizada (`cabeca`), eixo dos olhos + encolher + mola das orelhas (`corpo`), pupilas/piscar/rabo (`extra`), e o rig chibi (`rig1`, `rig2`, `rig3`, `bracos` = alvo das patas). A física (molas, piscar, pupilas) é calculada em `atualizarBichinho()`.
- **Efeitos são independentes, um por vez** (`EstadoPainel::efeito`, troca com fade):
  - **Luz reativa**: nuvem de pontos neon (`nuvemDePontos()`: interior esparso + contorno denso), aditiva sobre preto; orbes nas mãos opcionais. O usuário pediu para tirar a névoa de fundo, o halo, as linhas de esqueleto e os pontos de junta; não os traga de volta sem pedido.
  - **Bichinho espelho** (para crianças): personagem chibi (gato, dinossauro, coelho) com **proporções fixas** (cabeça do tamanho do corpo, corpo de feijãozinho, membros curtos com patas redondas), sobre fundo colorido com bolhas. Retargeting como no Animated Drawings da Meta: da pessoa só vêm posição, escala (largura dos ombros, limitada para caber na tela) e os ÂNGULOS dos membros, calculados em `atualizarRig()`; nunca os comprimentos reais (isso dava corpo de minhoca). A silhueta da máscara NÃO é usada (o usuário achou feio). Personagens com menos de ~55% do tamanho do maior em cena são descartados (detecções falsas). Braços com **IK de dois ossos** no shader: o C++ manda só o alvo da pata (direção ombro→pulso × quanto o braço real está esticado); o cotovelo é resolvido e dobra sempre para fora. Movimento com **dinâmica de segunda ordem** (`DinamicaSegundaOrdem.h`, t3ssel8r: frequência/amortecimento/resposta) em raiz, escala, cabeça (mais mole: atrasa e balança), inclinação, corpo (inclina para onde anda), patas e pernas; os parâmetros de cada parte ficam no `Slot` em `ofApp.h`. Rabo é uma corrente de Verlet estilo spring bone do VRM (`raboPontos`). O lado dos braços é decidido pela posição dos ombros na tela com histerese (o modelo vê a imagem espelhada e inverte E/D). Squash & stretch pela velocidade vertical; cabeça deslocada pela mola em `rig3.yz`. O usuário pediu para remover as asas; não as traga de volta.
  - **Ecos em quadrinhos**: a pessoa vira HQ — cor chapada (bilateral + 3 tons) e traço XDoG (Winnemöller 2012), calculados em `ProcessadorVisao::estilizarQuadrinho()` só quando o efeito está ativo (parâmetros `XDOG_*` no config) — com retícula Ben-Day nas sombras, sobre fundo pop-art com raios. Ecos = histórico das máscaras (`atualizarEcos()`, até 8 ecos em duas texturas RGBA, um por canal) em cores pop com retícula que encolhe com a idade; na batida, um "carimbo" branco da silhueta. O usuário pediu para tirar as onomatopeias/balões ("POW!") e não quis os ecos em pontos.
  - **Salto de paraquedas**: cena de desenho (céu, nuvens, sol, morro) desenhada com o OF em `Paraquedistas.cpp`; o avião vem **assim que alguém olha para a câmera** (olhos e nariz visíveis) e cada pessoa salta de novo só após `intervaloAviao` s. Cada paraquedista tem o **rosto real da pessoa**: `ofApp::recortarRostos()` recorta 256x256 do quadro da câmera centrado entre olhos e nariz (tamanho pela distância dos olhos), gira para ficar reto, aplica CLAHE no L do Lab e nitidez; só atualiza com o rosto de frente (senão guarda o último). Queda livre → velame abre com mola → desce balançando → pousa e some logo. O processador só envia o quadro (`definirEnviarQuadro`) quando o efeito está ativo. O usuário não quis o traço neon nem bonecos de palitinho aqui; a ideia é a cara das pessoas nos paraquedistas.
  - **LEGO**: mosaico de peças 1x2 vistas de cima (`cenaLego()` no shader): presença pela máscara no centro de cada peça, cor da câmera (`cameraTexture`, com ganho automático pelo brilho médio da pessoa — `ganhoCamera`) ajustada à paleta LEGO (inclui tons de pele). Peças de onde a pessoa saiu caem com gravidade usando o histórico de silhuetas dos ecos (passo de 2 quadros). Tamanho das peças e "peças caem" no painel.
- `EstadoPainel.h`: estado compartilhado entre as duas janelas (mesma thread do OF, sem trava).
- `PainelApp`: painel desenhado à mão (cartões, interruptores, sliders, medidores), sem ofxGui; grade de medidas fixa no topo do arquivo, rolagem com a roda do mouse, janela redimensionável, textos cortados com "…" para não invadir controles. Fontes Noto Sans em `of_void_cloud/bin/data/`. Botões disparam ao **soltar** o clique, ignoram o primeiro segundo e repetições em <150 ms, porque o X11 entrega clique fantasma ao abrir e cliques duplicados.

## Dependências vendorizadas (`vendor/`)

`vendor/` não vai para o git (2,6 GB, arquivos > 100 MB): `./instalar_dependencias.sh` baixa tudo. Dos modelos, só `yolo26n-seg.onnx` e `yolo26s-pose.onnx` são versionados. O usuário pediu para remover o efeito Cabeçudos (boneco de Olinda / cabeça gigante); não o traga de volta.


- `openframeworks/` (`OF_ROOT` em `of_void_cloud/config.make`).
- `onnxruntime-linux-x64-gpu_cuda13-1.30.0/` (release oficial do GitHub) e `cuda/lib/` (cudart, cublas, curand e cuDNN 9, extraídos das wheels `nvidia-*` do PyPI).
- O provider CUDA do ORT é carregado por `dlopen` e o `libcublas` não tem rpath. Por isso o link usa `-Wl,--disable-new-dtags` (DT_RPATH, que vale para dependências indiretas) com `$ORIGIN/../../vendor/...`. Quem mexer em `config.make` ou no script do benchmark precisa manter isso, senão o ORT cai silenciosamente para CPU (o log mostra "CUDA indisponível").
- OpenCV 4.6 do sistema (`core`, `imgproc`, `videoio`, `ximgproc`), incluído com `-isystem` para silenciar warnings dos headers em C++20.

## Desempenho medido (GTX 1660 Ti)

seg-n + pose-s ≈ 17 ms de inferência e ≈ 22 ms por quadro com composição, contra o limite de 33 ms a 30 fps. FP16 ficou mais lento e a conversão quebra o head end-to-end, então mantenha FP32 nesta GPU. `modelos/rvm_mobilenetv3_fp16.onnx` é da pipeline antiga e não é mais usado.
