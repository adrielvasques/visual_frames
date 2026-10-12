#pragma once

// Cena "Salto de paraquedas": um avião de desenho cruza o céu e solta
// paraquedistas que têm o ROSTO das pessoas na frente da câmera (recortado ao
// vivo do quadro). Queda livre, o paraquedas abre com um tranco, descem
// balançando, pousam no morro, acenam e somem. Desenhado com o OF.

#include "ofMain.h"

#include <map>
#include <vector>

// Rosto de uma pessoa, já recortado, endireitado e com a luz corrigida
// (textura quadrada com o rosto centralizado).
struct RostoCamera {
    int id = -1;                       // slot da pessoa
    float presenca = 0;
    const ofTexture* textura = nullptr;
    bool deFrente = false;             // olhando para a câmera agora
};

class Paraquedistas {
public:
    void atualizar(float dt, const std::vector<RostoCamera>& rostos, float nivelMusica, float intervaloAviao);
    void desenhar(float largura, float altura, const std::vector<RostoCamera>& rostos, float opacidade) const;
    void limpar();

private:
    enum class Fase { QuedaLivre, Paraquedas, NoChao };

    struct Saltador {
        Fase fase = Fase::QuedaLivre;
        glm::vec2 posicao{0, 0};    // centro do corpo, uv da tela
        glm::vec2 velocidade{0, 0}; // uv/s
        float tempoNaFase = 0;
        float abertura = 0;         // velame: 0 fechado .. 1 aberto (com "tranco")
        float velocidadeAbertura = 0;
        float semente = 0;
        int rosto = -1;             // id (slot) da pessoa cujo rosto ele usa
        ofColor macacao;
        ofColor velame1;
        ofColor velame2;
        float sumir = 0;            // 0..1 no fim
    };

    struct Aviao {
        bool ativo = false;
        float x = 0;
        float direcao = 1;
        float helice = 0;
        std::vector<int> fila;      // rostos que ainda vão saltar nesta passagem
        std::vector<float> pontosDeSalto;
    };

    struct Nuvem {
        glm::vec2 posicao{0, 0};
        float escala = 1;
        float velocidade = 0.01f;
        float camada = 0;           // 0 = longe, 1 = perto
    };

    void desenharCeu(float largura, float altura, float opacidade) const;
    void desenharNuvens(float largura, float altura, float opacidade, bool frente) const;
    void desenharChao(float largura, float altura, float opacidade) const;
    void desenharAviao(float largura, float altura, float opacidade) const;
    void desenharSaltador(const Saltador& s, float largura, float altura, const std::vector<RostoCamera>& rostos,
                          float opacidade) const;

    std::vector<Saltador> saltadores;
    std::vector<Nuvem> nuvens;
    Aviao aviao;
    std::map<int, float> ultimoSalto; // id da pessoa -> relógio do último salto
    float ultimaPassagem = -100;
    float relogio = 0;
};
