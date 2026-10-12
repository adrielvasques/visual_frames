#pragma once

// Inferência YOLO26 (segmentação + pose) com ONNX Runtime CUDA.
// Independente do OpenFrameworks para poder ser medido isoladamente.

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

constexpr int NUM_KEYPOINTS = 17; // ordem COCO

enum Keypoint {
    NARIZ, OLHO_E, OLHO_D, ORELHA_E, ORELHA_D,
    OMBRO_E, OMBRO_D, COTOVELO_E, COTOVELO_D, PULSO_E, PULSO_D,
    QUADRIL_E, QUADRIL_D, JOELHO_E, JOELHO_D, TORNOZELO_E, TORNOZELO_D,
};

struct PontoChave {
    float x = 0;          // pixels da câmera
    float y = 0;
    float confianca = 0;  // visibilidade (sigmoid) do modelo
};

struct DeteccaoPessoa {
    cv::Rect2f caixa;     // pixels da câmera
    float confianca = 0;
    bool temPose = false;
    std::array<PontoChave, NUM_KEYPOINTS> pontos{};
    // Logits da máscara (CV_32F) só dentro de regiaoMascara, que está em
    // coordenadas MASCARA_LARGURA x MASCARA_ALTURA; vazio quando a pessoa
    // veio só da pose.
    cv::Mat logitsMascara;
    cv::Rect regiaoMascara;
};

struct TemposDeteccao {
    double preprocessamentoMs = 0;
    double segmentacaoMs = 0;
    double poseMs = 0;
    double posprocessamentoMs = 0;
};

class DetectorPessoas {
public:
    DetectorPessoas(const std::string& modeloSegmentacao, const std::string& modeloPose);
    ~DetectorPessoas();

    // frameBgr deve ter CAMERA_LARGURA x CAMERA_ALTURA.
    std::vector<DeteccaoPessoa> detectar(const cv::Mat& frameBgr);

    bool usandoCuda() const { return cuda; }
    const TemposDeteccao& tempos() const { return ultimosTempos; }

private:
    struct Implementacao;
    std::unique_ptr<Implementacao> impl;
    bool cuda = false;
    TemposDeteccao ultimosTempos;
};

float iou(const cv::Rect2f& a, const cv::Rect2f& b);
