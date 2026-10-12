#include "AnalisadorAudio.h"

#include "Configuracao.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <iostream>
#include <vector>

#include <kiss_fftr.h>
#include <pulse/error.h>
#include <pulse/simple.h>

namespace {

constexpr float ANALISES_POR_SEGUNDO = static_cast<float>(config::AUDIO_TAXA) / config::AUDIO_SALTO;
const int HISTORICO_BATIDA = static_cast<int>(std::round(ANALISES_POR_SEGUNDO)); // ~1 s

// Ganho automático + suavização (sobe rápido, desce devagar).
struct Banda {
    float pico = 1e-9f;
    float nivel = 0;

    float atualizar(float energia, float decaimentoPico) {
        pico = std::max({energia, pico * decaimentoPico, 1e-9f});
        const float alvo = std::sqrt(energia / pico); // amplitude relativa, mais perceptual
        nivel += (alvo > nivel ? 0.6f : 0.12f) * (alvo - nivel);
        return nivel;
    }
};

int bin(float hz) {
    return std::clamp(
        static_cast<int>(std::round(hz * config::AUDIO_JANELA_FFT / config::AUDIO_TAXA)),
        1, config::AUDIO_JANELA_FFT / 2
    );
}

float energiaBanda(const std::vector<float>& potencia, int inicio, int fim) {
    float soma = 0;
    for (int k = inicio; k < fim; ++k) {
        soma += potencia[k];
    }
    return soma / std::max(1, fim - inicio);
}

} // namespace

AnalisadorAudio::AnalisadorAudio(std::string dispositivo) : dispositivo(std::move(dispositivo)) {}

AnalisadorAudio::~AnalisadorAudio() {
    parar();
}

void AnalisadorAudio::iniciar() {
    rodando = true;
    thread = std::thread(&AnalisadorAudio::laco, this);
}

void AnalisadorAudio::parar() {
    rodando = false;
    if (thread.joinable()) {
        thread.join();
    }
}

NiveisAudio AnalisadorAudio::niveis() const {
    return {grave, medio, agudo, geral, batidas, ativo};
}

void AnalisadorAudio::laco() {
    const pa_sample_spec formato{PA_SAMPLE_FLOAT32LE, config::AUDIO_TAXA, 1};
    // fragsize pequeno = blocos chegam a cada salto, sem acumular latência.
    pa_buffer_attr buffer{};
    buffer.maxlength = static_cast<uint32_t>(-1);
    buffer.fragsize = config::AUDIO_SALTO * sizeof(float);

    const int janela = config::AUDIO_JANELA_FFT;
    kiss_fftr_cfg fft = kiss_fftr_alloc(janela, 0, nullptr, nullptr);
    std::vector<float> amostras(janela, 0.0f);
    std::vector<float> bloco(config::AUDIO_SALTO);
    std::vector<float> janelado(janela);
    std::vector<kiss_fft_cpx> espectro(janela / 2 + 1);
    std::vector<float> potencia(janela / 2 + 1);
    std::vector<float> hann(janela);
    for (int i = 0; i < janela; ++i) {
        hann[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * i / (janela - 1));
    }

    const int fimGrave = bin(config::AUDIO_GRAVE_MAX_HZ);
    const int fimMedio = bin(config::AUDIO_MEDIO_MAX_HZ);
    const int fimAgudo = bin(config::AUDIO_AGUDO_MAX_HZ);
    const float decaimentoPico = std::pow(0.5f, 1.0f / (config::AUDIO_MEIA_VIDA_PICO_S * ANALISES_POR_SEGUNDO));
    const int refratario = static_cast<int>(config::BATIDA_REFRATARIO_S * ANALISES_POR_SEGUNDO);

    Banda bandaGrave;
    Banda bandaMedio;
    Banda bandaAgudo;
    Banda bandaGeral;
    std::deque<float> historicoGrave;
    int desdeUltimaBatida = refratario;

    while (rodando) {
        int erro = 0;
        pa_simple* fluxo = pa_simple_new(
            nullptr, "visual_frames", PA_STREAM_RECORD,
            dispositivo.empty() ? nullptr : dispositivo.c_str(),
            "analise de musica", &formato, nullptr, &buffer, &erro
        );
        if (fluxo == nullptr) {
            std::cerr << "Áudio indisponível (" << dispositivo << "): " << pa_strerror(erro) << "\n";
            ativo = false;
            for (int i = 0; i < 20 && rodando; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }
        std::cout << "Áudio conectado: " << (dispositivo.empty() ? "entrada padrão" : dispositivo) << "\n";

        while (rodando) {
            if (pa_simple_read(fluxo, bloco.data(), bloco.size() * sizeof(float), &erro) < 0) {
                std::cerr << "Leitura de áudio falhou: " << pa_strerror(erro) << "\n";
                break;
            }
            // Janela deslizante: 2048 amostras, avança 1024 por leitura.
            std::copy(amostras.begin() + bloco.size(), amostras.end(), amostras.begin());
            std::copy(bloco.begin(), bloco.end(), amostras.end() - bloco.size());

            float somaQuadrados = 0;
            for (float amostra : bloco) {
                somaQuadrados += amostra * amostra;
            }
            const float rms = std::sqrt(somaQuadrados / bloco.size());
            if (rms < config::AUDIO_SILENCIO_RMS) {
                // Silêncio: deixa os níveis caírem e não conta batidas.
                ativo = false;
                grave = bandaGrave.atualizar(0, decaimentoPico);
                medio = bandaMedio.atualizar(0, decaimentoPico);
                agudo = bandaAgudo.atualizar(0, decaimentoPico);
                geral = bandaGeral.atualizar(0, decaimentoPico);
                historicoGrave.clear();
                continue;
            }
            ativo = true;

            for (int i = 0; i < janela; ++i) {
                janelado[i] = amostras[i] * hann[i];
            }
            kiss_fftr(fft, janelado.data(), espectro.data());
            for (std::size_t k = 0; k < espectro.size(); ++k) {
                potencia[k] = espectro[k].r * espectro[k].r + espectro[k].i * espectro[k].i;
            }

            const float energiaGrave = energiaBanda(potencia, 1, fimGrave);
            grave = bandaGrave.atualizar(energiaGrave, decaimentoPico);
            medio = bandaMedio.atualizar(energiaBanda(potencia, fimGrave, fimMedio), decaimentoPico);
            agudo = bandaAgudo.atualizar(energiaBanda(potencia, fimMedio, fimAgudo), decaimentoPico);
            geral = bandaGeral.atualizar(rms * rms, decaimentoPico);

            // Batida: energia do grave contra a média do último ~1 s. Patin
            // reduz C quando a variância é alta; aqui a variância é relativa
            // (desvio/média) para não depender do volume.
            desdeUltimaBatida++;
            if (static_cast<int>(historicoGrave.size()) >= HISTORICO_BATIDA) {
                float media = 0;
                for (float e : historicoGrave) {
                    media += e;
                }
                media /= historicoGrave.size();
                float variancia = 0;
                for (float e : historicoGrave) {
                    variancia += (e - media) * (e - media);
                }
                const float coeficienteVariacao = std::sqrt(variancia / historicoGrave.size()) / std::max(media, 1e-12f);
                const float c = std::clamp(
                    config::BATIDA_C_MAX - 0.3f * coeficienteVariacao, config::BATIDA_C_MIN, config::BATIDA_C_MAX
                );
                if (energiaGrave > c * media && desdeUltimaBatida >= refratario) {
                    batidas++;
                    desdeUltimaBatida = 0;
                }
                historicoGrave.pop_front();
            }
            historicoGrave.push_back(energiaGrave);
        }
        pa_simple_free(fluxo);
        ativo = false;
    }
    kiss_fftr_free(fft);
}
