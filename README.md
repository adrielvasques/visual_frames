# Visual Frames

Instalação de projeção interativa: uma webcam vê as pessoas e um app em
openFrameworks (C++/GLSL) projeta efeitos que reagem ao corpo, ao rosto e à
música. Funciona com várias pessoas ao mesmo tempo.

A visão roda na GPU: YOLO26 de segmentação e de pose (ONNX Runtime + CUDA),
rastreamento estilo ByteTrack e keypoints suavizados com One Euro.

## Efeitos

| Tecla | Efeito | O que faz |
|---|---|---|
| `1` | **Luz reativa** | Pontos neon no contorno da pessoa; as cores seguem a música. |
| `2` | **Bichinho espelho** | A pessoa vira um personagem de desenho (gato, dinossauro ou coelho) que imita os movimentos. |
| `3` | **Ecos em quadrinhos** | A pessoa vira HQ (traço de nanquim e retícula) e deixa ecos coloridos para trás. |
| `4` | **Salto de paraquedas** | Um avião solta paraquedistas com o rosto de quem está olhando para a câmera. |
| `5` | **LEGO** | A pessoa vira um mosaico de peças LEGO; ao se mexer, as peças se soltam e caem. |

O app abre duas janelas: a **Projeção** e o **Painel de controle**, onde se
escolhe o efeito e os ajustes.

## Requisitos

- Linux (testado no Ubuntu 24.04), GPU NVIDIA com driver compatível com CUDA 13.
- Webcam. O caminho padrão é o de uma Logitech C920. Para outra câmera, troque
  `CAMERA_DISPOSITIVO` em `of_void_cloud/src/Configuracao.h`.
- Pacotes: `libopencv-dev libopencv-contrib-dev libpulse-dev python3-pip`.

## Instalação

```bash
sudo apt install libopencv-dev libopencv-contrib-dev libpulse-dev python3-pip
./instalar_dependencias.sh
```

O script baixa para `vendor/` (fora do git, cerca de 2,6 GB):
- openFrameworks 0.12.1, já compilando a biblioteca;
- ONNX Runtime 1.30.0 para CUDA 13;
- bibliotecas CUDA e cuDNN.

Se a compilação do openFrameworks falhar, instale as dependências dele e rode
o script de novo:

```bash
sudo vendor/openframeworks/scripts/linux/ubuntu/install_dependencies.sh
./instalar_dependencias.sh
```

## Uso

```bash
./executar_void_gpu.sh                                # compila se preciso e abre
VISUAL_FRAMES_EFEITO=lego ./executar_void_gpu.sh      # abre direto num efeito (bichinho|ecos|paraquedas|lego)
VISUAL_FRAMES_MONITOR=1 ./executar_void_gpu.sh        # projeção em tela cheia no monitor 1 (HDMI)
VISUAL_FRAMES_DEBUG=1 ./executar_void_gpu.sh          # modo debug (câmera, esqueleto, tempos)
```

Teclas na projeção: `1` a `5` trocam o efeito, `d` liga o debug, `f` alterna a
tela cheia e `q` ou `Esc` fecha o app.

Para a música controlar as cores, basta tocar algo no próprio computador: o
app captura o áudio da saída padrão.

## Estrutura

- `of_void_cloud/src/`: código do app. Os parâmetros ajustáveis ficam em
  `Configuracao.h`.
- `modelos/`: os modelos YOLO26 em ONNX.
- `CLAUDE.md`: notas de arquitetura e das decisões de implementação.
