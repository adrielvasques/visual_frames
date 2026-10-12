// Mede a latência do DetectorPessoas numa imagem (ou na câmera, sem imagem).
// Uso: ./benchmark <seg.onnx> <pose.onnx> [imagem.jpg] [saida_debug.jpg]

#include "../src/Configuracao.h"
#include "../src/DetectorPessoas.h"

#include <chrono>
#include <iostream>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "uso: " << argv[0] << " <seg.onnx> <pose.onnx> [imagem] [saida.jpg]\n";
        return 1;
    }
    cv::Mat frame;
    if (argc >= 4) {
        cv::resize(cv::imread(argv[3]), frame, {config::CAMERA_LARGURA, config::CAMERA_ALTURA});
    } else {
        cv::VideoCapture camera(config::CAMERA_DISPOSITIVO, cv::CAP_V4L2);
        camera.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        camera.set(cv::CAP_PROP_FRAME_WIDTH, config::CAMERA_LARGURA);
        camera.set(cv::CAP_PROP_FRAME_HEIGHT, config::CAMERA_ALTURA);
        camera >> frame;
    }
    if (frame.empty()) {
        std::cerr << "sem imagem\n";
        return 1;
    }

    auto inicio = std::chrono::steady_clock::now();
    DetectorPessoas detector(argv[1], argv[2]);
    std::vector<DeteccaoPessoa> pessoas = detector.detectar(frame); // aquecimento
    std::cout << "abertura + 1a execução: "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - inicio).count()
              << " s, CUDA=" << detector.usandoCuda() << "\n";

    constexpr int REPETICOES = 100;
    TemposDeteccao soma;
    inicio = std::chrono::steady_clock::now();
    for (int i = 0; i < REPETICOES; ++i) {
        pessoas = detector.detectar(frame);
        soma.preprocessamentoMs += detector.tempos().preprocessamentoMs;
        soma.segmentacaoMs += detector.tempos().segmentacaoMs;
        soma.poseMs += detector.tempos().poseMs;
        soma.posprocessamentoMs += detector.tempos().posprocessamentoMs;
    }
    const double total = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - inicio).count() / REPETICOES;
    std::cout << "por quadro: " << total << " ms (pre " << soma.preprocessamentoMs / REPETICOES
              << ", seg " << soma.segmentacaoMs / REPETICOES
              << ", pose " << soma.poseMs / REPETICOES
              << ", pos " << soma.posprocessamentoMs / REPETICOES << ")\n";

    cv::Mat debug = frame.clone();
    for (const auto& p : pessoas) {
        std::cout << "pessoa conf=" << p.confianca << " pose=" << p.temPose
                  << " mascara=" << !p.logitsMascara.empty() << " caixa=" << p.caixa << "\n";
        if (!p.logitsMascara.empty()) {
            cv::Mat completa(config::MASCARA_ALTURA, config::MASCARA_LARGURA, CV_8U, cv::Scalar(0));
            completa(p.regiaoMascara).setTo(255, p.logitsMascara > 0);
            cv::Mat mascara;
            cv::resize(completa, mascara, frame.size(), 0, 0, cv::INTER_NEAREST);
            debug.setTo(cv::Scalar(20, 200, 60), mascara);
        }
        cv::rectangle(debug, p.caixa, {0, 255, 255}, 2);
        for (const auto& k : p.pontos) {
            if (k.confianca > config::LIMIAR_KEYPOINT) {
                cv::circle(debug, {cvRound(k.x), cvRound(k.y)}, 5, {0, 0, 255}, -1);
            }
        }
    }
    if (argc >= 5) {
        cv::imwrite(argv[4], debug);
    }
}
