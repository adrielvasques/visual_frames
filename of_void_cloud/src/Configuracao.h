#pragma once

// Parâmetros editáveis da instalação. Valores de partida vêm das referências
// citadas ao lado; ajuste observando a cena real (tecla 'd' mostra o debug).

#include <string>

namespace config {

// --- Câmera (Logitech C920, MJPG via V4L2) ---------------------------------
inline const std::string CAMERA_DISPOSITIVO =
    "/dev/v4l/by-id/usb-046d_HD_Pro_Webcam_C920_64BA017F-video-index0";
constexpr int CAMERA_LARGURA = 1280;
constexpr int CAMERA_ALTURA = 720;
constexpr int CAMERA_FPS = 30;
constexpr bool CAMERA_ESPELHAR = true;

// --- Modelos YOLO26 (exportados com imgsz=(384,640), nms=False, FP32) -------
// Entrada retangular 640x384: 1280x720 vira 640x360 + 12 px de borda em cima
// e embaixo, ~40% menos pixels que 640x640 sem perder escala.
// Medido na GTX 1660 Ti (bin/benchmark), seg+pose em paralelo:
//   n+n 17.7 ms | n-seg + s-pose 21.3 ms | s+s 25.7 ms
// A pose precisa mais de precisão (ancora as ilusões) do que a máscara.
// FP16 ficou mais lento nesta GPU (Turing GTX sem Tensor Cores).
// Caminhos relativos ao executável (of_void_cloud/bin).
inline const std::string MODELO_SEGMENTACAO = "../../modelos/yolo26n-seg.onnx";
inline const std::string MODELO_POSE = "../../modelos/yolo26s-pose.onnx";
constexpr int ENTRADA_LARGURA = 640;
constexpr int ENTRADA_ALTURA = 384;

// --- Detecção / rastreamento (padrões do bytetrack.yaml do Ultralytics) ----
constexpr float LIMIAR_DETECCAO_ALTO = 0.25f;  // track_high_thresh
constexpr float LIMIAR_DETECCAO_BAIXO = 0.10f; // track_low_thresh
constexpr float LIMIAR_NOVA_PESSOA = 0.25f;    // new_track_thresh
constexpr int QUADROS_MEMORIA_PERDIDA = 30;    // track_buffer (1 s a 30 fps)
constexpr float IOU_MINIMO_ASSOCIACAO = 0.20f; // 1 - match_thresh (0.8)
constexpr float IOU_MINIMO_SEGUNDA_ETAPA = 0.50f;
constexpr float IOU_POSE_SEGMENTACAO = 0.45f;  // casa caixa da pose com a da máscara
// O head end-to-end do YOLO26 dispensa NMS, mas ainda pode duplicar uma pessoa;
// supressão leve com o iou padrão do Ultralytics (0.7).
constexpr float IOU_DUPLICATA = 0.70f;
// Não cria pessoa nova se ela e uma pessoa ativa se sobrepõem nessa fração da
// menor das duas caixas (pega duplicatas maiores e menores que o original).
constexpr float CONTENCAO_DUPLICATA = 0.80f;
// Ignora pessoas muito pequenas (longe/fundo); fração da altura do quadro.
constexpr float ALTURA_MINIMA_PESSOA = 0.12f;
constexpr int MAX_PESSOAS = 6;

// --- Keypoints ----------------------------------------------------------------
// Visibilidade abaixo disso não atualiza o ponto (ele "segura" e esmaece).
constexpr float LIMIAR_KEYPOINT = 0.50f;
// One Euro Filter (Casiez et al., CHI 2012), coordenadas em pixels da câmera.
// Jitter parado -> baixe MIN_CUTOFF; atraso em movimento rápido -> suba BETA.
constexpr float EURO_MIN_CUTOFF = 1.0f;
constexpr float EURO_BETA = 0.02f;
constexpr float EURO_D_CUTOFF = 1.0f;
// Tempo (s) para um keypoint perdido sumir no shader.
constexpr float KEYPOINT_DECAIMENTO_S = 0.35f;

// --- Máscara -----------------------------------------------------------------
// Logits do protótipo (1/4 da entrada) são ampliados para 640x360 e refinados
// com filtro guiado pela imagem da câmera, que gruda a borda no contorno real.
constexpr int MASCARA_LARGURA = 640;
constexpr int MASCARA_ALTURA = 360;
constexpr bool MASCARA_FILTRO_GUIADO = true;
constexpr int FILTRO_GUIADO_RAIO = 4;
constexpr double FILTRO_GUIADO_EPS = 0.01 * 255.0 * 255.0; // (0.1 em [0,1])^2
// Mistura temporal: peso do quadro atual (1.0 = sem suavização).
constexpr float MASCARA_PESO_QUADRO_ATUAL = 0.65f;
// Margem (pixels de entrada) ao recortar a máscara pela caixa da pessoa.
constexpr float MASCARA_MARGEM_CAIXA = 4.0f;

// --- Áudio (cores reagem à música) --------------------------------------------
// "@DEFAULT_MONITOR@" = o que está tocando neste PC (monitor da saída padrão
// no PipeWire/PulseAudio). Para usar o microfone padrão, troque por "".
inline const std::string AUDIO_DISPOSITIVO = "@DEFAULT_MONITOR@";
constexpr int AUDIO_TAXA = 48000;
constexpr int AUDIO_JANELA_FFT = 2048;   // ~23 Hz por bin
constexpr int AUDIO_SALTO = 1024;        // ~47 análises/s
// Bandas (Hz): grave = bumbo/baixo, médio = voz/harmonia, agudo = pratos/brilho.
constexpr float AUDIO_GRAVE_MAX_HZ = 150.0f;
constexpr float AUDIO_MEDIO_MAX_HZ = 2000.0f;
constexpr float AUDIO_AGUDO_MAX_HZ = 10000.0f;
// Ganho automático: o pico de cada banda cai pela metade nesse tempo, então
// a reação se adapta ao volume e ao estilo da música.
constexpr float AUDIO_MEIA_VIDA_PICO_S = 4.0f;
constexpr float AUDIO_SILENCIO_RMS = 1e-4f; // ~-80 dBFS
// Batida (Patin, "Beat Detection Algorithms"): energia do grave > C x média
// do último ~1 s. C cai quando a energia varia muito (batidas marcadas).
constexpr float BATIDA_C_MAX = 1.45f;
constexpr float BATIDA_C_MIN = 1.15f;
constexpr float BATIDA_REFRATARIO_S = 0.22f; // até ~270 BPM

// --- Ecos em quadrinhos: traço XDoG (Winnemöller et al., 2012) -----------------
// sigma: espessura do traço (px na resolução 640x360); k: razão entre as duas
// gaussianas (1.6 é o clássico); tau: peso da gaussiana larga (mais perto de 1
// = menos linhas); eps/phi: limiar e dureza do traço.
constexpr double XDOG_SIGMA = 1.0;
constexpr double XDOG_K = 1.6;
constexpr float XDOG_TAU = 0.98f;
constexpr float XDOG_EPS = 0.0f;
constexpr float XDOG_PHI = 120.0f;

} // namespace config
