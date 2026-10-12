#pragma once

// Estado compartilhado entre a janela de projeção (ofApp) e a janela do painel
// de controle (PainelApp). As duas rodam na mesma thread principal do OF, então
// os campos podem ser lidos e escritos direto, sem trava.

#include <string>

// Efeitos independentes: só um fica ativo por vez.
enum class Efeito { Luz = 0, Bichinho = 1, Ecos = 2, Paraquedas = 3, Lego = 4 };
enum class Animal { Gato = 0, Dinossauro = 1, Coelho = 2 };

struct EstadoPainel {
    Efeito efeito = Efeito::Luz;

    // Efeito "Luz reativa" (nuvem de pontos neon que reage à música)
    bool orbesMaos = false;
    bool corSegueMusica = true;
    float matizFixo = 0.75f;      // 0..1
    float sensibilidadeSom = 1.0f; // 0.2..3

    // Efeito "Bichinho espelho"
    Animal animal = Animal::Gato;
    bool fundoColorido = true;

    // Efeito "Ecos em quadrinhos"
    float quantidadeEcos = 6;  // 1..8
    float distanciaEcos = 4;   // quadros da câmera entre um eco e o próximo (1..8)

    // Efeito "Salto de paraquedas"
    float intervaloAviao = 10; // segundos entre uma passagem do avião e a próxima (3..20)

    // Efeito "LEGO"
    float tamanhoPeca = 16;   // lado do pino em px a 720p (10..28)
    bool legoCair = true;     // peças se soltam e caem quando a pessoa se mexe


    // Comum a todos
    float brilho = 1.0f; // 0.3..1.6

    // Só leitura para o painel (preenchidos pelo ofApp)
    float visaoMs = 0;
    bool usandoCuda = false;
    int pessoas = 0;
    bool audioAtivo = false;
    float grave = 0;
    float medio = 0;
    float agudo = 0;
    bool encerrar = false; // o ofApp pede para o painel fechar junto
};
