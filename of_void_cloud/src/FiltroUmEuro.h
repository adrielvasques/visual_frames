#pragma once

// One Euro Filter (Casiez, Roussel, Vogel — CHI 2012): passa-baixa cujo corte
// sobe com a velocidade. Parado = muito suave; rápido = pouco atraso.

#include <cmath>

class FiltroUmEuro {
public:
    FiltroUmEuro(float minCutoff = 1.0f, float beta = 0.0f, float dCutoff = 1.0f)
        : minCutoff(minCutoff), beta(beta), dCutoff(dCutoff) {}

    float filtrar(float valor, float dt) {
        if (!iniciado || dt <= 0.0f) {
            iniciado = true;
            anterior = valor;
            derivada = 0.0f;
            return valor;
        }
        const float derivadaBruta = (valor - anterior) / dt;
        derivada += alfa(dCutoff, dt) * (derivadaBruta - derivada);
        const float corte = minCutoff + beta * std::fabs(derivada);
        anterior += alfa(corte, dt) * (valor - anterior);
        return anterior;
    }

    void reiniciar() { iniciado = false; }
    float velocidade() const { return derivada; } // unidades/s, já suavizada

private:
    static float alfa(float corte, float dt) {
        const float tau = 1.0f / (2.0f * 3.14159265f * corte);
        return 1.0f / (1.0f + tau / dt);
    }

    float minCutoff;
    float beta;
    float dCutoff;
    bool iniciado = false;
    float anterior = 0.0f;
    float derivada = 0.0f;
};
