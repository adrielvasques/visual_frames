#pragma once

// Thread de visão: câmera -> DetectorPessoas -> RastreadorPessoas -> máscara
// composta. O renderer só copia o último ResultadoVisao publicado.

#include "Configuracao.h"
#include "DetectorPessoas.h"
#include "RastreadorPessoas.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

struct PessoaVisivel {
    int id = 0;
    int slot = 0;
    bool ativa = false;            // detectada neste quadro
    cv::Rect2f caixa;              // pixels da câmera
    std::array<PontoChave, NUM_KEYPOINTS> pontos{}; // confianca já com decaimento (0..1)
    float velocidadePulsoE = 0;    // alturas de corpo / s
    float velocidadePulsoD = 0;
};

struct ResultadoVisao {
    std::uint64_t sequencia = 0;
    // MASCARA_LARGURA x MASCARA_ALTURA, CV_8UC2: [alpha 0..255, slot+1 (0 = ninguém)]
    cv::Mat mascara;
    std::vector<PessoaVisivel> pessoas;
    cv::Mat frameDebug;            // BGR da câmera (espelhada), só no debug ou no efeito Paraquedas
    // Imagem em estilo HQ (MASCARA_LARGURA x MASCARA_ALTURA, CV_8UC4): RGB com
    // cores chapadas e A = traço de nanquim (255 = tinta). Só no efeito Ecos.
    cv::Mat quadrinho;
    TemposDeteccao tempos;
    double composicaoMs = 0;
    double quadroMs = 0;
    bool usandoCuda = false;
};

class ProcessadorVisao {
public:
    ProcessadorVisao(std::string modeloSegmentacao, std::string modeloPose);
    ~ProcessadorVisao();

    void iniciar();
    void parar();

    // Copia o resultado se houver um mais novo que `sequenciaConhecida`.
    bool obterSeNovo(std::uint64_t sequenciaConhecida, ResultadoVisao& destino);

    std::string estado() const;
    void definirDebug(bool ligado) { debug = ligado; }
    void definirQuadrinho(bool ligado) { quadrinho = ligado; }
    void definirEnviarQuadro(bool ligado) { enviarQuadro = ligado; }

private:
    void laco();
    void comporMascara(const std::vector<DeteccaoPessoa>& deteccoes, const cv::Mat& frame);
    void publicar(const cv::Mat& frame, double quadroMs);
    cv::Mat estilizarQuadrinho(const cv::Mat& frame) const;

    std::string caminhoSegmentacao;
    std::string caminhoPose;
    std::thread thread;
    std::atomic<bool> rodando{false};
    std::atomic<bool> debug{false};
    std::atomic<bool> quadrinho{false};
    std::atomic<bool> enviarQuadro{false};

    std::unique_ptr<DetectorPessoas> detector;
    RastreadorPessoas rastreador;

    // Buffers da composição (só a thread de visão mexe).
    cv::Mat melhorLogit;      // CV_32F
    cv::Mat slotAtual;        // CV_8U
    cv::Mat slotAnterior;     // CV_8U
    cv::Mat alpha;            // CV_32F
    cv::Mat alphaSuave;       // CV_32F
    cv::Mat guia;             // CV_8U, câmera em cinza na resolução da máscara
    double composicaoMs = 0;

    mutable std::mutex trava;
    ResultadoVisao ultimo;
    std::string mensagemEstado = "Carregando modelos...";
};
