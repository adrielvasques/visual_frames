#pragma once

// Rastreamento multi-pessoa no estilo ByteTrack (Zhang et al., ECCV 2022):
// associa primeiro detecções confiáveis, depois tenta salvar trilhas com as
// detecções fracas (oclusão, borrão). Sem Kalman: a 30 fps uma predição de
// velocidade constante da caixa basta para o IoU casar.

#include "Configuracao.h"
#include "DetectorPessoas.h"
#include "FiltroUmEuro.h"

#include <array>
#include <vector>

struct PessoaRastreada {
    int id = 0;
    int slot = 0;                 // 0..MAX_PESSOAS-1, estável durante a vida
    cv::Rect2f caixa;
    cv::Point2f velocidadeCaixa;  // px/quadro
    float confianca = 0;
    int quadrosPerdida = 0;
    int idade = 0;
    int deteccao = -1;            // índice na lista do quadro atual, -1 se perdida

    // Keypoints suavizados (pixels da câmera) e quão "frescos" estão (0..1).
    std::array<PontoChave, NUM_KEYPOINTS> pontos{};
    std::array<float, NUM_KEYPOINTS> segundosSemVer{};
    std::array<FiltroUmEuro, NUM_KEYPOINTS> filtroX;
    std::array<FiltroUmEuro, NUM_KEYPOINTS> filtroY;

    // Velocidade de cada pulso em alturas-de-corpo por segundo (escala
    // independente da distância até a câmera).
    float velocidadePulsoE = 0;
    float velocidadePulsoD = 0;
};

class RastreadorPessoas {
public:
    // dt em segundos desde o quadro anterior.
    void atualizar(const std::vector<DeteccaoPessoa>& deteccoes, float dt);
    const std::vector<PessoaRastreada>& pessoas() const { return trilhas; }

private:
    PessoaRastreada criarTrilha(const DeteccaoPessoa& deteccao, int slot);
    void aplicarDeteccao(PessoaRastreada& trilha, const DeteccaoPessoa& deteccao, int indice, float dt);
    int slotLivre() const;

    std::vector<PessoaRastreada> trilhas;
    int proximoId = 1;
};
