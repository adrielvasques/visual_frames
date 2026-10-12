#pragma once

// Captura o áudio (PulseAudio/PipeWire) numa thread e extrai níveis por banda
// e batidas. Os níveis já vêm normalizados em 0..1 pelo ganho automático.

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

struct NiveisAudio {
    float grave = 0;
    float medio = 0;
    float agudo = 0;
    float geral = 0;
    std::uint32_t batidas = 0; // contador; mudou = houve batida nova
    bool ativo = false;        // false = sem dispositivo ou em silêncio
};

class AnalisadorAudio {
public:
    explicit AnalisadorAudio(std::string dispositivo);
    ~AnalisadorAudio();

    void iniciar();
    void parar();
    NiveisAudio niveis() const;

private:
    void laco();

    std::string dispositivo;
    std::thread thread;
    std::atomic<bool> rodando{false};

    std::atomic<float> grave{0};
    std::atomic<float> medio{0};
    std::atomic<float> agudo{0};
    std::atomic<float> geral{0};
    std::atomic<std::uint32_t> batidas{0};
    std::atomic<bool> ativo{false};
};
