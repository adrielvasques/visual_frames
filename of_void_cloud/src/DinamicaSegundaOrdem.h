#pragma once

// Dinâmica de segunda ordem (t3ssel8r, "Giving Personality to Procedural
// Animations using Math"): segue um alvo como um sistema massa-mola-amortecedor.
//   f: frequência natural (Hz) — quão rápido responde;
//   z: amortecimento — < 1 balança e passa do ponto, 1 crítico, > 1 lento;
//   r: resposta inicial — 0 começa devagar, 1 imediato, > 1 antecipa, < 0 recua.
// Dá inércia, follow-through e acomodação, coisa que uma média exponencial
// (sem massa) não dá.

#include <algorithm>
#include <cmath>

template <typename T>
class DinamicaSegundaOrdem {
public:
    DinamicaSegundaOrdem() = default;
    DinamicaSegundaOrdem(float f, float z, float r, T inicial = T(0)) { configurar(f, z, r, inicial); }

    void configurar(float f, float z, float r, T inicial) {
        constexpr float PI_F = 3.14159265f;
        k1 = z / (PI_F * f);
        k2 = 1.0f / ((2.0f * PI_F * f) * (2.0f * PI_F * f));
        k3 = r * z / (2.0f * PI_F * f);
        reiniciar(inicial);
    }

    void reiniciar(T valor) {
        alvoAnterior = valor;
        y = valor;
        velocidade = T(0);
    }

    const T& atualizar(float dt, const T& alvo) {
        if (dt <= 0) {
            return y;
        }
        const T velocidadeAlvo = (alvo - alvoAnterior) / dt;
        alvoAnterior = alvo;
        // k2 mínimo para manter a integração estável com dt grande.
        const float k2Estavel = std::max({k2, dt * dt / 2.0f + dt * k1 / 2.0f, dt * k1});
        y = y + velocidade * dt;
        velocidade = velocidade + (alvo + velocidadeAlvo * k3 - y - velocidade * k1) * (dt / k2Estavel);
        return y;
    }

    const T& valor() const { return y; }
    const T& velocidadeAtual() const { return velocidade; }

private:
    float k1 = 0;
    float k2 = 1;
    float k3 = 0;
    T alvoAnterior{};
    T y{};
    T velocidade{};
};
