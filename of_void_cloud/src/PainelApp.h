#pragma once

#include "ofMain.h"

#include "EstadoPainel.h"

#include <functional>
#include <memory>
#include <vector>

// Janela de controle: fica no notebook enquanto a projeção ocupa o HDMI.
// Desenhada à mão (cartões, interruptores, sliders) em vez de usar o ofxGui.
class PainelApp : public ofBaseApp {
public:
    explicit PainelApp(std::shared_ptr<EstadoPainel> estado) : estado(std::move(estado)) {}

    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void mousePressed(int x, int y, int botao) override;
    void mouseDragged(int x, int y, int botao) override;
    void mouseReleased(int x, int y, int botao) override;
    void mouseMoved(int x, int y) override;
    void mouseScrolled(int x, int y, float scrollX, float scrollY) override;

private:
    struct Alvo {
        ofRectangle area;
        std::function<void(float, float)> aoPressionar; // x, y em coordenadas da janela
        std::function<void(float, float)> aoArrastar;
    };

    enum class Icone { Luz, Bichinho, Ecos, Neon, Lego, Gato, Dinossauro, Coelho };

    // Widgets (modo imediato: desenham e registram a área clicável a cada quadro).
    float cabecalho(float y);
    float secao(const std::string& titulo, float y);
    float cartaoEfeito(float y, Efeito efeito, const std::string& titulo, const std::string& descricao);
    void botaoAnimal(const ofRectangle& area, Animal animal, const std::string& nome);
    float interruptor(float y, const std::string& rotulo, const std::string& dica, bool& valor);
    float controleDeslizante(
        float y, const std::string& rotulo, float& valor, float minimo, float maximo,
        const std::string& texto, bool arcoIris = false, bool desativado = false
    );
    float medidores(float y);
    void desenharIcone(Icone icone, const ofRectangle& area, const ofColor& cor, float pulso);
    void escrever(ofTrueTypeFont& fonte, const std::string& texto, float x, float y, const ofColor& cor);
    void escreverCentralizado(ofTrueTypeFont& fonte, const std::string& texto, const ofRectangle& area, const ofColor& cor);
    bool sobMouse(const ofRectangle& area) const { return mouse.y < areaVisivel() && area.inside(mouse.x, mouse.y + rolagem); }
    std::string caber(ofTrueTypeFont& fonte, const std::string& texto, float largura);
    float larguraUtil() const;
    float areaVisivel() const;
    ofColor acento() const;

    std::shared_ptr<EstadoPainel> estado;
    ofTrueTypeFont fonteTitulo;
    ofTrueTypeFont fonteNegrito;
    ofTrueTypeFont fonteTexto;
    ofTrueTypeFont fontePequena;

    std::vector<Alvo> alvos;
    int arrastando = -1;
    ofRectangle pressionado;   // botão onde o clique começou
    float ultimoDisparo = -1;
    glm::vec2 mouse{-1, -1};

    // Estado visual suavizado.
    float destaqueAtivo = 1;
    Efeito efeitoAnterior = Efeito::Luz;
    float nivelGrave = 0;
    float nivelMedio = 0;
    float nivelAgudo = 0;

    float rolagem = 0;
    float alturaConteudo = 0;
};
