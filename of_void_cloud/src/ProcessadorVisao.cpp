#include "ProcessadorVisao.h"

#include <chrono>
#include <iostream>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/ximgproc.hpp>

namespace {

using Relogio = std::chrono::steady_clock;

constexpr float LOGIT_VAZIO = -20.0f;

double msDesde(Relogio::time_point inicio) {
    return std::chrono::duration<double, std::milli>(Relogio::now() - inicio).count();
}

bool abrirCamera(cv::VideoCapture& camera) {
    camera.open(config::CAMERA_DISPOSITIVO, cv::CAP_V4L2);
    if (!camera.isOpened()) {
        return false;
    }
    camera.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    camera.set(cv::CAP_PROP_FRAME_WIDTH, config::CAMERA_LARGURA);
    camera.set(cv::CAP_PROP_FRAME_HEIGHT, config::CAMERA_ALTURA);
    camera.set(cv::CAP_PROP_FPS, config::CAMERA_FPS);
    camera.set(cv::CAP_PROP_BUFFERSIZE, 1);
    return true;
}

} // namespace

ProcessadorVisao::ProcessadorVisao(std::string modeloSegmentacao, std::string modeloPose)
    : caminhoSegmentacao(std::move(modeloSegmentacao)), caminhoPose(std::move(modeloPose)) {
    const cv::Size tamanho(config::MASCARA_LARGURA, config::MASCARA_ALTURA);
    melhorLogit.create(tamanho, CV_32F);
    slotAtual.create(tamanho, CV_8U);
    slotAnterior = cv::Mat::zeros(tamanho, CV_8U);
    alphaSuave = cv::Mat::zeros(tamanho, CV_32F);
}

ProcessadorVisao::~ProcessadorVisao() {
    parar();
}

void ProcessadorVisao::iniciar() {
    rodando = true;
    thread = std::thread(&ProcessadorVisao::laco, this);
}

void ProcessadorVisao::parar() {
    rodando = false;
    if (thread.joinable()) {
        thread.join();
    }
}

bool ProcessadorVisao::obterSeNovo(std::uint64_t sequenciaConhecida, ResultadoVisao& destino) {
    std::lock_guard<std::mutex> guarda(trava);
    if (ultimo.sequencia <= sequenciaConhecida) {
        return false;
    }
    destino = ultimo; // os cv::Mat publicados nunca são reescritos, compartilhar é seguro
    return true;
}

std::string ProcessadorVisao::estado() const {
    std::lock_guard<std::mutex> guarda(trava);
    return mensagemEstado;
}

void ProcessadorVisao::laco() {
    try {
        detector = std::make_unique<DetectorPessoas>(caminhoSegmentacao, caminhoPose);
    } catch (const std::exception& erro) {
        std::lock_guard<std::mutex> guarda(trava);
        mensagemEstado = std::string("Falha ao carregar modelos: ") + erro.what();
        std::cerr << mensagemEstado << "\n";
        return;
    }
    std::cout << "Detector em " << (detector->usandoCuda() ? "CUDA" : "CPU") << "\n";
    {
        std::lock_guard<std::mutex> guarda(trava);
        mensagemEstado = "Abrindo câmera...";
    }

    cv::VideoCapture camera;
    cv::Mat frameBruto;
    cv::Mat frame;
    auto quadroAnterior = Relogio::now();

    while (rodando) {
        if (!camera.isOpened() && !abrirCamera(camera)) {
            {
                std::lock_guard<std::mutex> guarda(trava);
                mensagemEstado = "Câmera indisponível: " + config::CAMERA_DISPOSITIVO;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        if (!camera.read(frameBruto) || frameBruto.empty()) {
            // Acontece com o dispositivo ainda preso por outro processo.
            camera.release();
            {
                std::lock_guard<std::mutex> guarda(trava);
                mensagemEstado = "Sem quadros da câmera, tentando de novo...";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        const auto inicio = Relogio::now();
        if (frameBruto.cols != config::CAMERA_LARGURA || frameBruto.rows != config::CAMERA_ALTURA) {
            cv::resize(frameBruto, frameBruto, {config::CAMERA_LARGURA, config::CAMERA_ALTURA});
        }
        if (config::CAMERA_ESPELHAR) {
            cv::flip(frameBruto, frame, 1);
        } else {
            frame = frameBruto;
        }

        const float dt = std::chrono::duration<float>(inicio - quadroAnterior).count();
        quadroAnterior = inicio;

        const auto deteccoes = detector->detectar(frame);
        rastreador.atualizar(deteccoes, std::min(dt, 0.25f));
        comporMascara(deteccoes, frame);
        publicar(frame, msDesde(inicio));
    }
}

void ProcessadorVisao::comporMascara(const std::vector<DeteccaoPessoa>& deteccoes, const cv::Mat& frame) {
    const auto inicio = Relogio::now();
    melhorLogit.setTo(LOGIT_VAZIO);
    slotAtual.setTo(0);

    // Cada pixel fica com a pessoa de maior logit (resolve sobreposições).
    for (const auto& trilha : rastreador.pessoas()) {
        if (trilha.deteccao < 0) {
            continue;
        }
        const auto& deteccao = deteccoes[trilha.deteccao];
        if (deteccao.logitsMascara.empty()) {
            continue;
        }
        cv::Mat logitRegiao = melhorLogit(deteccao.regiaoMascara);
        cv::Mat slotRegiao = slotAtual(deteccao.regiaoMascara);
        const cv::Mat maior = deteccao.logitsMascara > logitRegiao;
        deteccao.logitsMascara.copyTo(logitRegiao, maior);
        slotRegiao.setTo(trilha.slot + 1, maior);
    }

    // alpha = sigmoid(logit)
    cv::exp(-melhorLogit, alpha);
    alpha = 1.0f / (1.0f + alpha);

    if (config::MASCARA_FILTRO_GUIADO) {
        // Filtro guiado (He et al., 2010): a borda da máscara passa a seguir
        // as bordas da imagem real, compensando o protótipo em 1/4 de resolução.
        cv::Mat reduzido;
        cv::resize(frame, reduzido, alpha.size(), 0, 0, cv::INTER_AREA);
        cv::cvtColor(reduzido, guia, cv::COLOR_BGR2GRAY);
        cv::ximgproc::guidedFilter(guia, alpha, alpha, config::FILTRO_GUIADO_RAIO, config::FILTRO_GUIADO_EPS);
        cv::max(alpha, 0.0f, alpha);
        cv::min(alpha, 1.0f, alpha);
    }

    cv::addWeighted(
        alpha, config::MASCARA_PESO_QUADRO_ATUAL,
        alphaSuave, 1.0f - config::MASCARA_PESO_QUADRO_ATUAL, 0.0, alphaSuave
    );
    // Onde ninguém foi visto agora, mantém o slot antigo enquanto o alpha
    // suavizado esmaece (evita a cor "piscar" para a de outra pessoa).
    slotAtual.copyTo(slotAnterior, slotAtual > 0);
    composicaoMs = msDesde(inicio);
}

void ProcessadorVisao::publicar(const cv::Mat& frame, double quadroMs) {
    ResultadoVisao resultado;
    cv::Mat alpha8;
    alphaSuave.convertTo(alpha8, CV_8U, 255.0);
    cv::merge(std::vector<cv::Mat>{alpha8, slotAnterior.clone()}, resultado.mascara);

    for (const auto& trilha : rastreador.pessoas()) {
        PessoaVisivel pessoa;
        pessoa.id = trilha.id;
        pessoa.slot = trilha.slot;
        pessoa.ativa = trilha.quadrosPerdida == 0;
        pessoa.caixa = trilha.caixa;
        for (int k = 0; k < NUM_KEYPOINTS; ++k) {
            pessoa.pontos[k] = trilha.pontos[k];
            const float frescor = 1.0f - trilha.segundosSemVer[k] / config::KEYPOINT_DECAIMENTO_S;
            pessoa.pontos[k].confianca = std::max(0.0f, frescor);
        }
        pessoa.velocidadePulsoE = trilha.velocidadePulsoE;
        pessoa.velocidadePulsoD = trilha.velocidadePulsoD;
        resultado.pessoas.push_back(pessoa);
    }
    if (debug || enviarQuadro) {
        resultado.frameDebug = frame.clone();
    }
    if (quadrinho) {
        resultado.quadrinho = estilizarQuadrinho(frame);
    }
    resultado.tempos = detector->tempos();
    resultado.composicaoMs = composicaoMs;
    resultado.quadroMs = quadroMs;
    resultado.usandoCuda = detector->usandoCuda();

    std::lock_guard<std::mutex> guarda(trava);
    resultado.sequencia = ultimo.sequencia + 1;
    ultimo = std::move(resultado);
    mensagemEstado.clear();
}

// Estilização de HQ (não-fotorrealista), em 640x360:
//  - traço: XDoG (Winnemöller, Kyprianidis & Olsen, 2012) — diferença de duas
//    gaussianas com limiar suave, o filtro clássico para "line art";
//  - cor: filtro bilateral (achata texturas e preserva bordas) + saturação
//    reforçada + brilho quantizado em 3 tons (cores chapadas de HQ).
cv::Mat ProcessadorVisao::estilizarQuadrinho(const cv::Mat& frame) const {
    const cv::Size tamanho(config::MASCARA_LARGURA, config::MASCARA_ALTURA);
    cv::Mat pequeno;
    cv::resize(frame, pequeno, tamanho, 0, 0, cv::INTER_AREA);

    // --- Traço (XDoG) ---
    cv::Mat cinza;
    cv::cvtColor(pequeno, cinza, cv::COLOR_BGR2GRAY);
    cinza.convertTo(cinza, CV_32F, 1.0 / 255.0);
    cv::Mat g1;
    cv::Mat g2;
    cv::GaussianBlur(cinza, g1, cv::Size(), config::XDOG_SIGMA);
    cv::GaussianBlur(cinza, g2, cv::Size(), config::XDOG_SIGMA * config::XDOG_K);
    cv::Mat d = g1 - config::XDOG_TAU * g2;
    // T(D) = 1 se D >= eps, senão 1 + tanh(phi (D - eps)); tinta = 1 - T.
    cv::Mat tinta(tamanho, CV_8U);
    for (int y = 0; y < d.rows; ++y) {
        const float* linhaD = d.ptr<float>(y);
        unsigned char* linhaT = tinta.ptr<unsigned char>(y);
        for (int x = 0; x < d.cols; ++x) {
            const float v = linhaD[x];
            const float t = v >= config::XDOG_EPS ? 1.0f : 1.0f + std::tanh(config::XDOG_PHI * (v - config::XDOG_EPS));
            linhaT[x] = cv::saturate_cast<unsigned char>((1.0f - t) * 255.0f);
        }
    }

    // --- Cor chapada ---
    cv::Mat suave;
    cv::bilateralFilter(pequeno, suave, 7, 60, 7);
    cv::Mat hsv;
    cv::cvtColor(suave, hsv, cv::COLOR_BGR2HSV);
    for (int y = 0; y < hsv.rows; ++y) {
        cv::Vec3b* linha = hsv.ptr<cv::Vec3b>(y);
        for (int x = 0; x < hsv.cols; ++x) {
            cv::Vec3b& p = linha[x];
            p[1] = cv::saturate_cast<unsigned char>(p[1] * 1.6f + 25);
            // 3 tons: sombra, meio-tom e luz.
            const int v = p[2];
            p[2] = static_cast<unsigned char>(v < 85 ? 70 : (v < 170 ? 165 : 245));
        }
    }
    cv::Mat chapado;
    cv::cvtColor(hsv, chapado, cv::COLOR_HSV2RGB);

    cv::Mat saida;
    cv::merge(std::vector<cv::Mat>{chapado, tinta}, saida); // RGB + A(tinta)
    return saida;
}
