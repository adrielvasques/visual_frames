#include "RastreadorPessoas.h"

#include <algorithm>
#include <cmath>

namespace {

struct Par {
    int trilha;
    int deteccao;
    float iou;
};

// Casamento guloso pelo maior IoU; com até MAX_PESSOAS trilhas o resultado é
// praticamente o mesmo do húngaro.
std::vector<Par> casar(
    const std::vector<PessoaRastreada>& trilhas, const std::vector<int>& indicesTrilhas,
    const std::vector<DeteccaoPessoa>& deteccoes, const std::vector<int>& indicesDeteccoes,
    float iouMinimo
) {
    std::vector<Par> candidatos;
    for (int t : indicesTrilhas) {
        for (int d : indicesDeteccoes) {
            const float valor = iou(trilhas[t].caixa, deteccoes[d].caixa);
            if (valor >= iouMinimo) {
                candidatos.push_back({t, d, valor});
            }
        }
    }
    std::sort(candidatos.begin(), candidatos.end(), [](const Par& a, const Par& b) {
        return a.iou > b.iou;
    });
    std::vector<Par> pares;
    std::vector<int> trilhasUsadas;
    std::vector<int> deteccoesUsadas;
    for (const auto& par : candidatos) {
        if (std::find(trilhasUsadas.begin(), trilhasUsadas.end(), par.trilha) != trilhasUsadas.end()
            || std::find(deteccoesUsadas.begin(), deteccoesUsadas.end(), par.deteccao) != deteccoesUsadas.end()) {
            continue;
        }
        trilhasUsadas.push_back(par.trilha);
        deteccoesUsadas.push_back(par.deteccao);
        pares.push_back(par);
    }
    return pares;
}

void remover(std::vector<int>& lista, int valor) {
    lista.erase(std::remove(lista.begin(), lista.end(), valor), lista.end());
}

} // namespace

void RastreadorPessoas::atualizar(const std::vector<DeteccaoPessoa>& deteccoes, float dt) {
    // Predição: caixa anda com a velocidade estimada.
    for (auto& trilha : trilhas) {
        trilha.caixa.x += trilha.velocidadeCaixa.x;
        trilha.caixa.y += trilha.velocidadeCaixa.y;
        trilha.deteccao = -1;
    }

    std::vector<int> altas;
    std::vector<int> baixas;
    for (int i = 0; i < static_cast<int>(deteccoes.size()); ++i) {
        (deteccoes[i].confianca >= config::LIMIAR_DETECCAO_ALTO ? altas : baixas).push_back(i);
    }

    // 1ª etapa: todas as trilhas (inclusive perdidas) contra detecções altas.
    std::vector<int> pendentes;
    for (int t = 0; t < static_cast<int>(trilhas.size()); ++t) {
        pendentes.push_back(t);
    }
    for (const auto& par : casar(trilhas, pendentes, deteccoes, altas, config::IOU_MINIMO_ASSOCIACAO)) {
        aplicarDeteccao(trilhas[par.trilha], deteccoes[par.deteccao], par.deteccao, dt);
        remover(pendentes, par.trilha);
        remover(altas, par.deteccao);
    }

    // 2ª etapa: trilhas que estavam ativas contra detecções fracas, com IoU
    // mais exigente (ByteTrack usa 0.5 aqui).
    std::vector<int> ativasPendentes;
    for (int t : pendentes) {
        if (trilhas[t].quadrosPerdida == 0) {
            ativasPendentes.push_back(t);
        }
    }
    for (const auto& par : casar(trilhas, ativasPendentes, deteccoes, baixas, config::IOU_MINIMO_SEGUNDA_ETAPA)) {
        aplicarDeteccao(trilhas[par.trilha], deteccoes[par.deteccao], par.deteccao, dt);
        remover(pendentes, par.trilha);
    }

    // Trilhas sem detecção envelhecem; keypoints ficam parados e esmaecem.
    for (int t : pendentes) {
        auto& trilha = trilhas[t];
        trilha.quadrosPerdida++;
        trilha.velocidadeCaixa *= 0.8f;
        for (auto& segundos : trilha.segundosSemVer) {
            segundos += dt;
        }
        trilha.velocidadePulsoE *= 0.8f;
        trilha.velocidadePulsoD *= 0.8f;
    }
    trilhas.erase(
        std::remove_if(trilhas.begin(), trilhas.end(), [](const PessoaRastreada& trilha) {
            return trilha.quadrosPerdida > config::QUADROS_MEMORIA_PERDIDA;
        }),
        trilhas.end()
    );

    // Detecções altas que sobraram viram pessoas novas, exceto quando são um
    // pedaço/cópia de alguém já rastreado neste quadro.
    for (int d : altas) {
        const cv::Rect2f& caixa = deteccoes[d].caixa;
        const bool contidaEmAtiva = std::any_of(trilhas.begin(), trilhas.end(), [&](const auto& trilha) {
            const float menorArea = std::min(trilha.caixa.area(), caixa.area());
            return trilha.quadrosPerdida == 0
                && (trilha.caixa & caixa).area() > config::CONTENCAO_DUPLICATA * menorArea;
        });
        if (deteccoes[d].confianca < config::LIMIAR_NOVA_PESSOA || contidaEmAtiva) {
            continue;
        }
        const int slot = slotLivre();
        if (slot < 0) {
            break;
        }
        trilhas.push_back(criarTrilha(deteccoes[d], slot));
        aplicarDeteccao(trilhas.back(), deteccoes[d], d, dt);
    }
}

PessoaRastreada RastreadorPessoas::criarTrilha(const DeteccaoPessoa& deteccao, int slot) {
    PessoaRastreada trilha;
    trilha.id = proximoId++;
    trilha.slot = slot;
    trilha.caixa = deteccao.caixa;
    for (int k = 0; k < NUM_KEYPOINTS; ++k) {
        trilha.filtroX[k] = FiltroUmEuro(config::EURO_MIN_CUTOFF, config::EURO_BETA, config::EURO_D_CUTOFF);
        trilha.filtroY[k] = FiltroUmEuro(config::EURO_MIN_CUTOFF, config::EURO_BETA, config::EURO_D_CUTOFF);
        trilha.segundosSemVer[k] = config::KEYPOINT_DECAIMENTO_S;
    }
    return trilha;
}

void RastreadorPessoas::aplicarDeteccao(
    PessoaRastreada& trilha, const DeteccaoPessoa& deteccao, int indice, float dt
) {
    if (trilha.idade > 0) {
        const cv::Point2f deslocamento =
            (deteccao.caixa.tl() + deteccao.caixa.br()) * 0.5f
            - (trilha.caixa.tl() + trilha.caixa.br()) * 0.5f
            + trilha.velocidadeCaixa; // caixa já foi movida pela predição
        trilha.velocidadeCaixa = trilha.velocidadeCaixa * 0.6f + deslocamento * 0.4f;
    }
    trilha.caixa = deteccao.caixa;
    trilha.confianca = deteccao.confianca;
    trilha.quadrosPerdida = 0;
    trilha.idade++;
    trilha.deteccao = indice;

    const float alturaCorpo = std::max(deteccao.caixa.height, 1.0f);
    for (int k = 0; k < NUM_KEYPOINTS; ++k) {
        const PontoChave& ponto = deteccao.pontos[k];
        if (!deteccao.temPose || ponto.confianca < config::LIMIAR_KEYPOINT) {
            trilha.segundosSemVer[k] += dt;
            continue;
        }
        if (trilha.segundosSemVer[k] >= config::KEYPOINT_DECAIMENTO_S) {
            // Sumiu por tempo demais: reaparece no lugar novo sem "deslizar".
            trilha.filtroX[k].reiniciar();
            trilha.filtroY[k].reiniciar();
        }
        trilha.pontos[k].x = trilha.filtroX[k].filtrar(ponto.x, dt);
        trilha.pontos[k].y = trilha.filtroY[k].filtrar(ponto.y, dt);
        trilha.pontos[k].confianca = ponto.confianca;
        trilha.segundosSemVer[k] = 0;
    }
    auto velocidade = [&](int k) {
        if (trilha.segundosSemVer[k] > 0) {
            return 0.0f;
        }
        return std::hypot(trilha.filtroX[k].velocidade(), trilha.filtroY[k].velocidade()) / alturaCorpo;
    };
    trilha.velocidadePulsoE = velocidade(PULSO_E);
    trilha.velocidadePulsoD = velocidade(PULSO_D);
}

int RastreadorPessoas::slotLivre() const {
    for (int slot = 0; slot < config::MAX_PESSOAS; ++slot) {
        const bool ocupado = std::any_of(trilhas.begin(), trilhas.end(), [slot](const auto& trilha) {
            return trilha.slot == slot;
        });
        if (!ocupado) {
            return slot;
        }
    }
    return -1;
}
