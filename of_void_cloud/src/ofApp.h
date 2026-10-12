#pragma once

#include "ofMain.h"

#include <opencv2/imgproc.hpp>

#include "AnalisadorAudio.h"
#include "Configuracao.h"
#include "DinamicaSegundaOrdem.h"
#include "Paraquedistas.h"
#include "EstadoPainel.h"
#include "ProcessadorVisao.h"

#include <array>
#include <deque>
#include <memory>

constexpr int NUM_ELOS_RABO = 8;

class ofApp : public ofBaseApp {
public:
    explicit ofApp(std::shared_ptr<EstadoPainel> estado) : estado(std::move(estado)) {}

    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void windowResized(int w, int h) override;
    void exit() override;

private:
    // Estado suavizado por slot, a 60 fps, para fade in/out sem saltos.
    struct Slot {
        int id = 0;
        float presenca = 0;
        float energiaE = 0;
        float energiaD = 0;
        float alturaCorpo = 0; // fração da altura do quadro
        std::array<PontoChave, NUM_KEYPOINTS> pontos{};

        // Bichinho: cabeça suavizada e encolhimento ao agachar.
        bool cabecaIniciada = false;
        glm::vec2 cabecaCentro{0.5f, 0.5f}; // uv
        float cabecaRaio = 0.05f;           // fração da altura da tela
        glm::vec2 cabecaDir{1.0f, 0.0f};    // eixo dos olhos (aponta para a direita da tela)
        float cabecaValida = 0;
        float alturaRef = 0;                // maior altura vista (pessoa em pé)
        float escala = 1;

        // Física e animação do rosto.
        glm::vec2 centroAnterior{0, 0};
        bool temCentroAnterior = false;
        float orelhaAngulo = 0;
        float orelhaVelocidade = 0;
        glm::vec2 pupila{0, 0};
        float piscar = 0;
        float relogioPiscar = 0;
        float proximoPiscar = 0;
        float raboAngulo = 0;
        float raboVelocidade = 0;
        float relogioRabo = 0;

        // Rig chibi (proporções fixas; só ângulos vêm da pessoa).
        bool rigIniciado = false;
        glm::vec2 rigRaiz{0.5f, 0.5f}; // centro do corpo, uv
        float rigU = 0;                // unidade do personagem, fração da altura
        float rigValido = 0;
        float rigCorpoAngulo = 0;
        float rigEsticar = 1;
        glm::vec2 alvoRaiz{0.5f, 0.5f};
        float alvoU = 0;
        glm::vec2 cabecaDeslocamento{0.0f, -0.98f}; // cabeça em relação à raiz, em U (eixos da tela)
        float cabecaInclinacao = 0;
        // Enquadramento do rosto no quadro da câmera (px), para o recorte.
        bool rostoIniciado = false;
        bool rostoDeFrente = false;
        glm::vec2 rostoCentro{0, 0};
        float rostoLado = 0;
        float rostoAngulo = 0;
        float ombroEaEsquerda = 0.5f; // 1 = keypoint OMBRO_E aparece à esquerda da tela
        std::array<glm::vec2, 2> mao{glm::vec2(-0.17f, 0.49f), glm::vec2(0.17f, 0.49f)}; // em U, a partir do ombro
        std::array<glm::vec2, 2> perna{glm::vec2(-0.15f, 0.99f), glm::vec2(0.15f, 0.99f)};

        // Dinâmicas de segunda ordem (f Hz, amortecimento, resposta inicial):
        // a "personalidade" de cada parte.
        DinamicaSegundaOrdem<float> dinU{1.2f, 1.0f, 0.0f};
        DinamicaSegundaOrdem<glm::vec2> dinRaiz{3.2f, 0.85f, 0.0f};
        DinamicaSegundaOrdem<glm::vec2> dinCabeca{2.4f, 0.45f, 0.0f};    // mais mole: atrasa e balança
        DinamicaSegundaOrdem<float> dinInclinacao{2.2f, 0.5f, 0.0f};
        DinamicaSegundaOrdem<float> dinCorpo{1.8f, 0.55f, 0.0f};
        DinamicaSegundaOrdem<float> dinEsticar{4.0f, 0.35f, 0.0f, 1.0f}; // squash que quica
        std::array<DinamicaSegundaOrdem<glm::vec2>, 2> dinMao{
            DinamicaSegundaOrdem<glm::vec2>{3.0f, 0.6f, 1.1f, glm::vec2(-0.17f, 0.49f)},
            DinamicaSegundaOrdem<glm::vec2>{3.0f, 0.6f, 1.1f, glm::vec2(0.17f, 0.49f)}};
        std::array<DinamicaSegundaOrdem<glm::vec2>, 2> dinPerna{
            DinamicaSegundaOrdem<glm::vec2>{3.0f, 0.7f, 0.0f, glm::vec2(-0.15f, 0.99f)},
            DinamicaSegundaOrdem<glm::vec2>{3.0f, 0.7f, 0.0f, glm::vec2(0.15f, 0.99f)}};

        // Rabo (corrente de Verlet), em pixels da câmera.
        bool raboIniciado = false;
        std::array<glm::vec2, NUM_ELOS_RABO> rabo{};
        std::array<glm::vec2, NUM_ELOS_RABO> raboAnterior{};
    };

    void criarShader();
    void atualizarQuad(int largura, int altura);
    void atualizarCursor();
    void atualizarSlots(float dt);
    void atualizarBichinho(Slot& slot, float dt);
    void atualizarRig(Slot& slot, float dt);
    void atualizarEstadoPainel();
    void atualizarMusica(float dt);
    void atualizarEcos();
    void atualizarCentroCena(float dt);
    void atualizarParaquedistas(float dt);
    void recortarRostos(const cv::Mat& quadro);
    void desenharEfeitos();
    void desenharDebug();

    std::unique_ptr<ProcessadorVisao> processador;
    std::unique_ptr<AnalisadorAudio> audio;
    NiveisAudio niveis;
    std::uint32_t ultimaBatida = 0;
    float matiz = 0.75f; // começa no violeta
    float flash = 0;
    ResultadoVisao resultado;
    std::array<Slot, config::MAX_PESSOAS> slots{};

    ofTexture mascaraTexture;
    ofTexture ecosATexture;
    ofTexture ecosBTexture;
    ofTexture carimboTexture;
    std::deque<cv::Mat> historicoMascaras; // mais novo na frente
    float carimboForca = 0;
    ofTexture quadrinhoTexture;
    glm::vec2 centroCena{0.5f, 0.5f};
    ofTexture cameraTexture;
    ofShader shaderEfeitos;
    ofMesh quadTela;

    std::shared_ptr<EstadoPainel> estado;

    bool debug = false;
    float efeitoMaos = 0;
    float efeitoNuvem = 1;
    float efeitoEcos = 0;
    float efeitoParaquedas = 0;
    float efeitoLego = 0;
    float ganhoCamera = 1;
    Paraquedistas paraquedistas;
    std::vector<RostoCamera> rostos;
    std::array<ofTexture, config::MAX_PESSOAS> texturasRosto;
    std::array<bool, config::MAX_PESSOAS> rostoPronto{};
    cv::Ptr<cv::CLAHE> clahe;
    bool quadroNovo = false;
    float efeitoBichinho = 0;
    float efeitoFundo = 1;
    float brilho = 1;
};
