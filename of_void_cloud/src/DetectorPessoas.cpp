#include "DetectorPessoas.h"

#include "Configuracao.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>

#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>

namespace {

using Relogio = std::chrono::steady_clock;

double msDesde(Relogio::time_point inicio) {
    return std::chrono::duration<double, std::milli>(Relogio::now() - inicio).count();
}

// Layout das saídas end-to-end (nms=False), conferido no head.py do
// Ultralytics: [x1, y1, x2, y2, conf, classe, extras...], ordenado por conf.
constexpr int CAMPOS_BASE = 6;
constexpr int COEFICIENTES_MASCARA = 32;
constexpr int CLASSE_PESSOA = 0;

} // namespace

float iou(const cv::Rect2f& a, const cv::Rect2f& b) {
    const float intersecao = (a & b).area();
    const float uniao = a.area() + b.area() - intersecao;
    return uniao > 0 ? intersecao / uniao : 0.0f;
}

namespace {

// Entrada já ordenada por confiança.
bool duplicada(const std::vector<DeteccaoPessoa>& mantidas, const cv::Rect2f& caixa) {
    return std::any_of(mantidas.begin(), mantidas.end(), [&](const DeteccaoPessoa& outra) {
        return iou(outra.caixa, caixa) > config::IOU_DUPLICATA;
    });
}

} // namespace

struct DetectorPessoas::Implementacao {
    Ort::Env ambiente{ORT_LOGGING_LEVEL_WARNING, "visual_frames"};
    std::unique_ptr<Ort::Session> segmentacao;
    std::unique_ptr<Ort::Session> pose;
    Ort::MemoryInfo memoriaCpu = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::vector<float> entrada;
    cv::Mat redimensionado;
    cv::Mat comBorda;
    cv::Mat emFloat;

    // Transformação câmera -> entrada letterbox.
    float escala = 1;
    int bordaX = 0;
    int bordaY = 0;
    int conteudoLargura = 0;
    int conteudoAltura = 0;

    std::unique_ptr<Ort::Session> criarSessao(const std::string& caminho, bool& cuda) {
        Ort::SessionOptions opcoes;
        opcoes.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        opcoes.SetIntraOpNumThreads(2);
        try {
            Ort::CUDAProviderOptions cudaOpcoes;
            // EXHAUSTIVE mede os algoritmos de convolução na primeira execução:
            // demora mais para abrir, mas a forma de entrada é fixa e o melhor
            // algoritmo fica escolhido para sempre.
            cudaOpcoes.Update({
                {"device_id", "0"},
                {"cudnn_conv_algo_search", "EXHAUSTIVE"},
                {"cudnn_conv_use_max_workspace", "1"},
                {"do_copy_in_default_stream", "1"},
            });
            opcoes.AppendExecutionProvider_CUDA_V2(*cudaOpcoes);
            cuda = true;
        } catch (const Ort::Exception& erro) {
            std::cerr << "CUDA indisponível, usando CPU: " << erro.what() << "\n";
            cuda = false;
        }
        return std::make_unique<Ort::Session>(ambiente, caminho.c_str(), opcoes);
    }

    void preparar() {
        escala = std::min(
            static_cast<float>(config::ENTRADA_LARGURA) / config::CAMERA_LARGURA,
            static_cast<float>(config::ENTRADA_ALTURA) / config::CAMERA_ALTURA
        );
        conteudoLargura = static_cast<int>(std::round(config::CAMERA_LARGURA * escala));
        conteudoAltura = static_cast<int>(std::round(config::CAMERA_ALTURA * escala));
        bordaX = (config::ENTRADA_LARGURA - conteudoLargura) / 2;
        bordaY = (config::ENTRADA_ALTURA - conteudoAltura) / 2;
        entrada.resize(3 * config::ENTRADA_LARGURA * config::ENTRADA_ALTURA);
    }

    void preprocessar(const cv::Mat& frameBgr) {
        cv::resize(frameBgr, redimensionado, {conteudoLargura, conteudoAltura}, 0, 0, cv::INTER_LINEAR);
        cv::copyMakeBorder(
            redimensionado, comBorda,
            bordaY, config::ENTRADA_ALTURA - conteudoAltura - bordaY,
            bordaX, config::ENTRADA_LARGURA - conteudoLargura - bordaX,
            cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114)
        );
        comBorda.convertTo(emFloat, CV_32FC3, 1.0 / 255.0);
        // HWC BGR -> CHW RGB direto no buffer do tensor.
        const int area = config::ENTRADA_LARGURA * config::ENTRADA_ALTURA;
        std::vector<cv::Mat> planos{
            cv::Mat(config::ENTRADA_ALTURA, config::ENTRADA_LARGURA, CV_32F, entrada.data() + 2 * area),
            cv::Mat(config::ENTRADA_ALTURA, config::ENTRADA_LARGURA, CV_32F, entrada.data() + area),
            cv::Mat(config::ENTRADA_ALTURA, config::ENTRADA_LARGURA, CV_32F, entrada.data()),
        };
        cv::split(emFloat, planos);
    }

    std::vector<Ort::Value> executar(Ort::Session& sessao) {
        const std::array<int64_t, 4> forma{1, 3, config::ENTRADA_ALTURA, config::ENTRADA_LARGURA};
        Ort::Value tensor = Ort::Value::CreateTensor<float>(
            memoriaCpu, entrada.data(), entrada.size(), forma.data(), forma.size()
        );
        Ort::AllocatorWithDefaultOptions alocador;
        const auto nomeEntrada = sessao.GetInputNameAllocated(0, alocador);
        std::vector<Ort::AllocatedStringPtr> nomesSaidaPtr;
        std::vector<const char*> nomesSaida;
        for (std::size_t i = 0; i < sessao.GetOutputCount(); ++i) {
            nomesSaidaPtr.push_back(sessao.GetOutputNameAllocated(i, alocador));
            nomesSaida.push_back(nomesSaidaPtr.back().get());
        }
        const char* nomes[] = {nomeEntrada.get()};
        return sessao.Run(
            Ort::RunOptions{nullptr}, nomes, &tensor, 1, nomesSaida.data(), nomesSaida.size()
        );
    }

    cv::Rect2f caixaParaCamera(const float* d) const {
        const float x1 = (d[0] - bordaX) / escala;
        const float y1 = (d[1] - bordaY) / escala;
        const float x2 = (d[2] - bordaX) / escala;
        const float y2 = (d[3] - bordaY) / escala;
        return cv::Rect2f(cv::Point2f(x1, y1), cv::Point2f(x2, y2));
    }

    bool caixaValida(const cv::Rect2f& caixa) const {
        return caixa.height >= config::ALTURA_MINIMA_PESSOA * config::CAMERA_ALTURA;
    }

    std::vector<DeteccaoPessoa> lerSegmentacao(std::vector<Ort::Value>& saidas) const {
        std::vector<DeteccaoPessoa> pessoas;
        const auto formaDet = saidas[0].GetTensorTypeAndShapeInfo().GetShape();
        const auto formaProto = saidas[1].GetTensorTypeAndShapeInfo().GetShape();
        const int linhas = static_cast<int>(formaDet[1]);
        const int campos = static_cast<int>(formaDet[2]);
        const int protoAltura = static_cast<int>(formaProto[2]);
        const int protoLargura = static_cast<int>(formaProto[3]);
        const float* det = saidas[0].GetTensorData<float>();
        const float* proto = saidas[1].GetTensorData<float>();
        std::vector<cv::Mat> planosProto;
        for (int c = 0; c < COEFICIENTES_MASCARA; ++c) {
            planosProto.emplace_back(
                protoAltura, protoLargura, CV_32F,
                const_cast<float*>(proto + c * protoAltura * protoLargura)
            );
        }
        // Região do protótipo que corresponde à imagem (sem a borda cinza).
        const float protoEscala = static_cast<float>(protoLargura) / config::ENTRADA_LARGURA;
        const cv::Rect regiaoConteudo(
            cvRound(bordaX * protoEscala), cvRound(bordaY * protoEscala),
            cvRound(conteudoLargura * protoEscala), cvRound(conteudoAltura * protoEscala)
        );
        const float mascaraEscala = static_cast<float>(config::MASCARA_LARGURA) / config::CAMERA_LARGURA;

        for (int i = 0; i < linhas && static_cast<int>(pessoas.size()) < 2 * config::MAX_PESSOAS; ++i) {
            const float* d = det + i * campos;
            if (d[4] < config::LIMIAR_DETECCAO_BAIXO) {
                break; // saída ordenada por confiança
            }
            if (static_cast<int>(d[5]) != CLASSE_PESSOA) {
                continue;
            }
            DeteccaoPessoa pessoa;
            pessoa.caixa = caixaParaCamera(d);
            pessoa.confianca = d[4];
            if (!caixaValida(pessoa.caixa) || duplicada(pessoas, pessoa.caixa)) {
                continue;
            }

            const float margem = config::MASCARA_MARGEM_CAIXA / escala;
            cv::Rect recorte(
                cv::Point(cvFloor((pessoa.caixa.x - margem) * mascaraEscala),
                          cvFloor((pessoa.caixa.y - margem) * mascaraEscala)),
                cv::Point(cvCeil((pessoa.caixa.br().x + margem) * mascaraEscala),
                          cvCeil((pessoa.caixa.br().y + margem) * mascaraEscala))
            );
            recorte &= cv::Rect(0, 0, config::MASCARA_LARGURA, config::MASCARA_ALTURA);
            if (recorte.empty()) {
                continue;
            }

            // Ultralytics process_mask: coeficientes @ protótipos, amplia
            // bilinear e só depois recorta (issue #24272). Aqui a ampliação é
            // feita direto na região da caixa, com a mesma convenção de meio
            // pixel do cv::resize, em vez de ampliar o protótipo inteiro.
            const double sx = static_cast<double>(regiaoConteudo.width) / config::MASCARA_LARGURA;
            const double sy = static_cast<double>(regiaoConteudo.height) / config::MASCARA_ALTURA;
            const double origemX = (recorte.x + 0.5) * sx - 0.5 + regiaoConteudo.x;
            const double origemY = (recorte.y + 0.5) * sy - 0.5 + regiaoConteudo.y;
            // Só a parte do protótipo que a caixa toca (+1 px para o bilinear).
            cv::Rect regiaoProto(
                cv::Point(cvFloor(origemX) - 1, cvFloor(origemY) - 1),
                cv::Point(cvCeil(origemX + recorte.width * sx) + 2, cvCeil(origemY + recorte.height * sy) + 2)
            );
            regiaoProto &= cv::Rect(0, 0, protoLargura, protoAltura);
            cv::Mat logits(regiaoProto.size(), CV_32F, cv::Scalar(0));
            for (int c = 0; c < COEFICIENTES_MASCARA; ++c) {
                cv::scaleAdd(planosProto[c](regiaoProto), d[CAMPOS_BASE + c], logits, logits);
            }
            const cv::Matx23d destinoParaOrigem(
                sx, 0, origemX - regiaoProto.x,
                0, sy, origemY - regiaoProto.y
            );
            cv::warpAffine(
                logits, pessoa.logitsMascara, destinoParaOrigem, recorte.size(),
                cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE
            );
            pessoa.regiaoMascara = recorte;
            pessoas.push_back(std::move(pessoa));
        }
        return pessoas;
    }

    std::vector<DeteccaoPessoa> lerPose(std::vector<Ort::Value>& saidas) const {
        std::vector<DeteccaoPessoa> pessoas;
        const auto forma = saidas[0].GetTensorTypeAndShapeInfo().GetShape();
        const int linhas = static_cast<int>(forma[1]);
        const int campos = static_cast<int>(forma[2]);
        const float* det = saidas[0].GetTensorData<float>();
        for (int i = 0; i < linhas && static_cast<int>(pessoas.size()) < 2 * config::MAX_PESSOAS; ++i) {
            const float* d = det + i * campos;
            if (d[4] < config::LIMIAR_DETECCAO_BAIXO) {
                break;
            }
            DeteccaoPessoa pessoa;
            pessoa.caixa = caixaParaCamera(d);
            pessoa.confianca = d[4];
            pessoa.temPose = true;
            if (!caixaValida(pessoa.caixa) || duplicada(pessoas, pessoa.caixa)) {
                continue;
            }
            for (int k = 0; k < NUM_KEYPOINTS; ++k) {
                const float* p = d + CAMPOS_BASE + 3 * k;
                pessoa.pontos[k] = {(p[0] - bordaX) / escala, (p[1] - bordaY) / escala, p[2]};
            }
            pessoas.push_back(std::move(pessoa));
        }
        return pessoas;
    }
};

DetectorPessoas::DetectorPessoas(const std::string& modeloSegmentacao, const std::string& modeloPose)
    : impl(std::make_unique<Implementacao>()) {
    bool cudaSegmentacao = false;
    bool cudaPose = false;
    impl->segmentacao = impl->criarSessao(modeloSegmentacao, cudaSegmentacao);
    impl->pose = impl->criarSessao(modeloPose, cudaPose);
    cuda = cudaSegmentacao && cudaPose;
    impl->preparar();
}

DetectorPessoas::~DetectorPessoas() = default;

std::vector<DeteccaoPessoa> DetectorPessoas::detectar(const cv::Mat& frameBgr) {
    auto inicio = Relogio::now();
    impl->preprocessar(frameBgr);
    ultimosTempos.preprocessamentoMs = msDesde(inicio);

    // As duas sessões leem o mesmo buffer de entrada e rodam em paralelo.
    auto inicioPose = Relogio::now();
    auto futuroPose = std::async(std::launch::async, [this, inicioPose] {
        auto saidas = impl->executar(*impl->pose);
        ultimosTempos.poseMs = msDesde(inicioPose);
        return saidas;
    });
    inicio = Relogio::now();
    auto saidasSegmentacao = impl->executar(*impl->segmentacao);
    ultimosTempos.segmentacaoMs = msDesde(inicio);
    auto saidasPose = futuroPose.get();

    inicio = Relogio::now();
    auto pessoas = impl->lerSegmentacao(saidasSegmentacao);
    auto poses = impl->lerPose(saidasPose);

    // Casamento guloso pose <-> máscara por IoU das caixas.
    std::vector<bool> poseUsada(poses.size(), false);
    for (auto& pessoa : pessoas) {
        int melhor = -1;
        float melhorIou = config::IOU_POSE_SEGMENTACAO;
        for (std::size_t j = 0; j < poses.size(); ++j) {
            if (poseUsada[j]) {
                continue;
            }
            const float valor = iou(pessoa.caixa, poses[j].caixa);
            if (valor > melhorIou) {
                melhorIou = valor;
                melhor = static_cast<int>(j);
            }
        }
        if (melhor >= 0) {
            poseUsada[melhor] = true;
            pessoa.temPose = true;
            pessoa.pontos = poses[melhor].pontos;
            pessoa.confianca = std::max(pessoa.confianca, poses[melhor].confianca);
        }
    }
    for (std::size_t j = 0; j < poses.size(); ++j) {
        if (!poseUsada[j]) {
            pessoas.push_back(std::move(poses[j]));
        }
    }
    std::sort(pessoas.begin(), pessoas.end(), [](const auto& a, const auto& b) {
        return a.confianca > b.confianca;
    });
    ultimosTempos.posprocessamentoMs = msDesde(inicio);
    return pessoas;
}
