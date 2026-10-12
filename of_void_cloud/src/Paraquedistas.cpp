#include "Paraquedistas.h"

#include "Configuracao.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float ALTURA_AVIAO = 0.14f;          // uv da tela
constexpr float CHAO = 0.90f;                  // onde os pés tocam o morro
constexpr float PES = 0.105f;                  // dos pés ao centro do corpo, uv (a 720p)
constexpr float TEMPO_QUEDA_LIVRE = 1.1f;      // s até abrir o paraquedas
constexpr float DESCIDA = 0.075f;              // uv/s com o paraquedas aberto
constexpr float TEMPO_NO_CHAO = 0.35f;         // s no chão antes de começar a sumir
const ofColor CONTORNO(28, 24, 44);

float aproximar(float atual, float alvo, float dt, float tau) {
    return atual + (alvo - atual) * (1.0f - std::exp(-dt / tau));
}

glm::vec2 girar(glm::vec2 p, float angulo) {
    const float c = std::cos(angulo);
    const float s = std::sin(angulo);
    return {c * p.x - s * p.y, s * p.x + c * p.y};
}

// Peça de desenho animado: contorno escuro + preenchimento. Retângulo +
// círculos porque o perfil core do OpenGL não tem linha grossa.
void capsula(glm::vec2 a, glm::vec2 b, float raio, const ofColor& cor, float alpha, float contorno) {
    const glm::vec2 d = b - a;
    const float comprimento = glm::length(d);
    const float angulo = std::atan2(d.y, d.x);
    for (int camada = 0; camada < 2; ++camada) {
        const float r = camada == 0 ? raio + contorno : raio;
        ofSetColor(camada == 0 ? CONTORNO : cor, alpha);
        ofDrawCircle(a, r);
        ofDrawCircle(b, r);
        if (comprimento > 0.5f) {
            ofPushMatrix();
            ofTranslate(a);
            ofRotateRad(angulo);
            ofDrawRectangle(0, -r, comprimento, 2 * r);
            ofPopMatrix();
        }
    }
}

void elipse(glm::vec2 c, float rx, float ry, const ofColor& cor, float alpha, float contorno) {
    ofSetColor(CONTORNO, alpha);
    ofDrawEllipse(c, 2 * (rx + contorno), 2 * (ry + contorno));
    ofSetColor(cor, alpha);
    ofDrawEllipse(c, 2 * rx, 2 * ry);
}

ofColor corAleatoria() {
    static const ofColor CORES[] = {
        {255, 92, 92}, {255, 168, 40}, {255, 214, 50}, {70, 200, 120},
        {60, 160, 255}, {160, 100, 255}, {255, 110, 190},
    };
    return CORES[static_cast<int>(ofRandom(7.0f)) % 7];
}

} // namespace

void Paraquedistas::limpar() {
    saltadores.clear();
    aviao = Aviao{};
    ultimoSalto.clear();
    ultimaPassagem = -100;
}

void Paraquedistas::atualizar(float dt, const std::vector<RostoCamera>& rostos, float nivelMusica, float intervaloAviao) {
    dt = std::min(dt, 1.0f / 20.0f);
    relogio += dt;

    // Nuvens em duas camadas (paralaxe), criadas uma vez.
    if (nuvens.empty()) {
        for (int i = 0; i < 9; ++i) {
            Nuvem n;
            n.camada = i < 5 ? 0.0f : 1.0f;
            n.posicao = {ofRandom(-0.1f, 1.1f), n.camada == 0 ? ofRandom(0.05f, 0.45f) : ofRandom(0.25f, 0.7f)};
            n.escala = n.camada == 0 ? ofRandom(0.6f, 0.9f) : ofRandom(1.0f, 1.4f);
            n.velocidade = n.camada == 0 ? ofRandom(0.006f, 0.012f) : ofRandom(0.015f, 0.025f);
            nuvens.push_back(n);
        }
    }
    for (Nuvem& n : nuvens) {
        n.posicao.x -= n.velocidade * dt;
        if (n.posicao.x < -0.25f) {
            n.posicao.x = 1.25f;
            n.posicao.y = n.camada == 0 ? ofRandom(0.05f, 0.45f) : ofRandom(0.25f, 0.7f);
        }
    }

    // Avião: vem na hora em que aparece alguém com o rosto visível; depois,
    // a mesma pessoa só salta de novo após o intervalo. Sem ninguém, de vez
    // em quando saltam dois "convidados".
    if (!aviao.ativo) {
        std::vector<int> prontos;
        for (const RostoCamera& r : rostos) {
            // Só salta quem está olhando para a câmera (rosto bom no recorte).
            if (r.presenca < 0.6f || r.textura == nullptr || !r.deFrente) {
                continue;
            }
            const auto salto = ultimoSalto.find(r.id);
            if (salto == ultimoSalto.end() || relogio - salto->second > intervaloAviao) {
                prontos.push_back(r.id);
            }
        }
        const bool convidados = rostos.empty() && relogio - ultimaPassagem > intervaloAviao * 1.5f;
        if (!prontos.empty() || convidados) {
            aviao.ativo = true;
            aviao.direcao = ofRandom(1.0f) < 0.5f ? 1.0f : -1.0f;
            aviao.x = aviao.direcao > 0 ? -0.2f : 1.2f;
            aviao.fila = prontos.empty() ? std::vector<int>{-1, -1} : prontos;
            for (int id : prontos) {
                ultimoSalto[id] = relogio;
            }
            ultimaPassagem = relogio;
            // Pontos de salto espalhados ao longo da passagem (o primeiro logo
            // depois que o avião entra na tela).
            aviao.pontosDeSalto.clear();
            const int n = static_cast<int>(aviao.fila.size());
            for (int i = 0; i < n; ++i) {
                const float t = (i + 0.5f) / n;
                const float x = 0.12f + 0.7f * t + ofRandom(-0.03f, 0.03f);
                aviao.pontosDeSalto.push_back(aviao.direcao > 0 ? x : 1.0f - x);
            }
        }
    } else {
        aviao.x += aviao.direcao * 0.22f * dt;
        aviao.helice += dt * 40.0f;
        while (!aviao.fila.empty()) {
            const float alvo = aviao.pontosDeSalto.front();
            const bool passou = aviao.direcao > 0 ? aviao.x >= alvo : aviao.x <= alvo;
            if (!passou || saltadores.size() >= 14) {
                break;
            }
            Saltador s;
            s.posicao = {aviao.x - aviao.direcao * 0.03f, ALTURA_AVIAO + 0.03f};
            s.velocidade = {aviao.direcao * 0.12f, 0.05f};
            s.semente = ofRandom(TWO_PI);
            s.rosto = aviao.fila.front();
            s.macacao = corAleatoria();
            s.velame1 = corAleatoria();
            s.velame2 = ofColor(255, 250, 240);
            saltadores.push_back(s);
            aviao.fila.erase(aviao.fila.begin());
            aviao.pontosDeSalto.erase(aviao.pontosDeSalto.begin());
        }
        if (aviao.x < -0.3f || aviao.x > 1.3f) {
            aviao.ativo = false;
        }
    }

    for (Saltador& s : saltadores) {
        s.tempoNaFase += dt;
        switch (s.fase) {
        case Fase::QuedaLivre:
            s.velocidade.y = std::min(s.velocidade.y + 1.5f * dt, 0.45f);
            s.velocidade.x = aproximar(s.velocidade.x, 0.0f, dt, 0.6f);
            if (s.tempoNaFase > TEMPO_QUEDA_LIVRE) {
                s.fase = Fase::Paraquedas;
                s.tempoNaFase = 0;
                s.velocidade.y = 0.02f; // tranco da abertura
            }
            break;
        case Fase::Paraquedas: {
            // Velame abre com mola (passa um pouco e volta: "pop").
            s.velocidadeAbertura += (90.0f * (1.0f - s.abertura) - 9.0f * s.velocidadeAbertura) * dt;
            s.abertura += s.velocidadeAbertura * dt;
            s.velocidade.y = aproximar(s.velocidade.y, DESCIDA, dt, 0.5f);
            s.velocidade.x = 0.03f * std::sin(relogio * 1.1f + s.semente);
            if (s.posicao.y + PES >= CHAO) {
                s.fase = Fase::NoChao;
                s.tempoNaFase = 0;
                s.posicao.y = CHAO - PES;
                s.velocidade = {0, 0};
            }
            break;
        }
        case Fase::NoChao:
            s.abertura = aproximar(s.abertura, 0.0f, dt, 0.35f);
            // Ao pousar, um aceno rápido e já vai sumindo.
            if (s.tempoNaFase > TEMPO_NO_CHAO) {
                s.sumir = std::min(1.0f, s.sumir + dt / 0.5f);
            }
            break;
        }
        s.posicao += s.velocidade * dt;
        s.posicao.x = std::clamp(s.posicao.x, 0.04f, 0.96f);
    }
    saltadores.erase(
        std::remove_if(saltadores.begin(), saltadores.end(), [](const Saltador& s) { return s.sumir >= 1.0f; }),
        saltadores.end()
    );
}

void Paraquedistas::desenhar(float largura, float altura, const std::vector<RostoCamera>& rostos, float opacidade) const {
    if (opacidade < 0.01f) {
        return;
    }
    ofPushStyle();
    ofEnableAlphaBlending();
    ofFill();
    desenharCeu(largura, altura, opacidade);
    // Todas as nuvens ficam atrás: nada pode cobrir os rostos.
    desenharNuvens(largura, altura, opacidade, false);
    desenharNuvens(largura, altura, opacidade, true);
    if (aviao.ativo) {
        desenharAviao(largura, altura, opacidade);
    }
    desenharChao(largura, altura, opacidade);
    // Quem está no chão por trás, quem está no ar na frente.
    for (const Saltador& s : saltadores) {
        if (s.fase == Fase::NoChao) {
            desenharSaltador(s, largura, altura, rostos, opacidade);
        }
    }
    for (const Saltador& s : saltadores) {
        if (s.fase != Fase::NoChao) {
            desenharSaltador(s, largura, altura, rostos, opacidade);
        }
    }
    ofPopStyle();
}

void Paraquedistas::desenharCeu(float largura, float altura, float opacidade) const {
    ofMesh ceu;
    ceu.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
    const ofFloatColor topo(0.27f, 0.56f, 0.93f, opacidade);
    const ofFloatColor baixo(0.70f, 0.88f, 1.0f, opacidade);
    ceu.addVertex({0, 0, 0});
    ceu.addColor(topo);
    ceu.addVertex({largura, 0, 0});
    ceu.addColor(topo);
    ceu.addVertex({0, altura, 0});
    ceu.addColor(baixo);
    ceu.addVertex({largura, altura, 0});
    ceu.addColor(baixo);
    ceu.draw();
    // Sol com brilho.
    const glm::vec2 sol(largura * 0.86f, altura * 0.16f);
    for (int i = 4; i >= 1; --i) {
        ofSetColor(255, 240, 150, 30 * opacidade);
        ofDrawCircle(sol, altura * (0.06f + 0.025f * i));
    }
    ofSetColor(255, 228, 90, 255 * opacidade);
    ofDrawCircle(sol, altura * 0.06f);
}

void Paraquedistas::desenharNuvens(float largura, float altura, float opacidade, bool frente) const {
    for (const Nuvem& n : nuvens) {
        if ((n.camada > 0.5f) != frente) {
            continue;
        }
        const glm::vec2 c(n.posicao.x * largura, n.posicao.y * altura);
        const float r = altura * 0.045f * n.escala;
        const float alpha = (frente ? 210.0f : 235.0f) * opacidade;
        ofSetColor(255, 255, 255, alpha);
        ofDrawCircle(c + glm::vec2(-1.6f * r, 0.3f * r), r * 0.9f);
        ofDrawCircle(c + glm::vec2(-0.5f * r, -0.3f * r), r * 1.25f);
        ofDrawCircle(c + glm::vec2(0.8f * r, -0.1f * r), r * 1.1f);
        ofDrawCircle(c + glm::vec2(1.8f * r, 0.35f * r), r * 0.8f);
        ofDrawRectRounded(c.x - 2.4f * r, c.y, 4.9f * r, 1.1f * r, r * 0.55f);
    }
}

void Paraquedistas::desenharChao(float largura, float altura, float opacidade) const {
    // Morro verde ondulado com uma faixa mais escura.
    for (int camada = 0; camada < 2; ++camada) {
        const float base = altura * (camada == 0 ? 0.885f : 0.93f);
        ofSetColor(camada == 0 ? ofColor(110, 200, 90) : ofColor(80, 165, 70), 255 * opacidade);
        ofBeginShape();
        ofVertex(0, altura);
        for (int i = 0; i <= 48; ++i) {
            const float x = largura * i / 48.0f;
            const float onda = std::sin(i * 0.35f + camada * 1.7f) * altura * 0.012f;
            ofVertex(x, base + onda);
        }
        ofVertex(largura, altura);
        ofEndShape(true);
    }
}

void Paraquedistas::desenharAviao(float largura, float altura, float opacidade) const {
    const float s = altura / 720.0f;
    const float a = 255 * opacidade;
    ofPushMatrix();
    ofTranslate(aviao.x * largura, ALTURA_AVIAO * altura + std::sin(relogio * 2.0f) * 4 * s);
    ofScale(aviao.direcao, 1);
    // Asa de trás, fuselagem, janelas, cauda, asa da frente e hélice.
    ofSetColor(CONTORNO, a);
    ofDrawEllipse(-10 * s, 8 * s, 128 * s, 34 * s);
    ofSetColor(200, 60, 60, a);
    ofDrawEllipse(-10 * s, 8 * s, 120 * s, 26 * s);
    capsula({-110 * s, 0}, {95 * s, 0}, 30 * s, ofColor(250, 250, 255), a, 3.5f * s);
    ofSetColor(CONTORNO, a);
    ofBeginShape();
    ofVertex(-140 * s, -62 * s);
    ofVertex(-95 * s, -10 * s);
    ofVertex(-130 * s, 0);
    ofEndShape(true);
    ofSetColor(220, 70, 70, a);
    ofBeginShape();
    ofVertex(-134 * s, -54 * s);
    ofVertex(-100 * s, -12 * s);
    ofVertex(-126 * s, -4 * s);
    ofEndShape(true);
    ofSetColor(220, 70, 70, a);
    ofDrawRectangle(-110 * s, 6 * s, 205 * s, 8 * s);
    for (int i = 0; i < 6; ++i) {
        ofSetColor(CONTORNO, a);
        ofDrawCircle((-60 + i * 26) * s, -8 * s, 8 * s);
        ofSetColor(140, 210, 255, a);
        ofDrawCircle((-60 + i * 26) * s, -8 * s, 6 * s);
    }
    ofSetColor(CONTORNO, a);
    ofDrawEllipse(5 * s, 22 * s, 120 * s, 22 * s);
    ofSetColor(230, 80, 80, a);
    ofDrawEllipse(5 * s, 22 * s, 112 * s, 15 * s);
    ofSetColor(CONTORNO, a);
    ofDrawCircle(126 * s, 0, 9 * s);
    ofSetColor(80, 80, 90, 180 * opacidade);
    ofDrawEllipse(130 * s, 0, 8 * s, 70 * s * std::abs(std::sin(aviao.helice)) + 8 * s);
    ofPopMatrix();
}

void Paraquedistas::desenharSaltador(
    const Saltador& sal, float largura, float altura, const std::vector<RostoCamera>& rostos, float opacidade
) const {
    const float s = altura / 720.0f;
    const float alpha = 255 * opacidade * (1.0f - sal.sumir);
    const glm::vec2 corpo(sal.posicao.x * largura, sal.posicao.y * altura);
    const float contorno = 3.0f * s;
    const ofColor pele(255, 214, 180);

    // Pose e inclinação conforme a fase (em coordenadas locais, y para baixo).
    float giro = 0;
    glm::vec2 maoE, maoD, peE, peD;
    const float tempo = relogio + sal.semente;
    if (sal.fase == Fase::QuedaLivre) {
        giro = 0.25f * std::sin(tempo * 5.0f);
        const float bate = 6 * std::sin(tempo * 14.0f);
        maoE = {-58, -30 + bate};
        maoD = {58, -30 - bate};
        peE = {-34, 62 - bate};
        peD = {34, 62 + bate};
    } else if (sal.fase == Fase::Paraquedas) {
        giro = 0.12f * std::sin(relogio * 1.1f + sal.semente);
        maoE = {-34, -66};
        maoD = {34, -66};
        const float balanca = 8 * std::sin(tempo * 2.5f);
        peE = {-14 + balanca, 66};
        peD = {14 + balanca, 64};
    } else {
        const float aceno = std::sin(tempo * 9.0f);
        maoE = {-34, 22};
        maoD = {48 + 6 * aceno, -48 + 8 * aceno};
        peE = {-16, 70};
        peD = {16, 70};
    }
    auto ponto = [&](glm::vec2 local) { return corpo + girar(local * s, giro); };

    // Velame com gomos listrados, borda recortada e cordas até os ombros.
    if (sal.abertura > 0.03f) {
        const float ab = sal.abertura;
        const glm::vec2 centro = ponto({0, -195});
        const float rx = 135 * s * ab;
        const float ry = 70 * s * std::min(ab, 1.2f);
        if (sal.fase == Fase::NoChao) {
            // Desinflando ao lado, no chão.
            ofSetColor(sal.velame1, alpha * 0.9f);
            ofDrawEllipse(corpo + glm::vec2(70 * s, 60 * s), rx * 1.6f, ry * 0.5f);
        } else {
            constexpr int GOMOS = 7;
            constexpr int PASSOS = 6;
            // Contorno só da cúpula (meia-elipse um pouco maior por trás).
            ofSetColor(CONTORNO, alpha);
            ofBeginShape();
            for (int p = 0; p <= 32; ++p) {
                const float a = PI + PI * p / 32.0f;
                ofVertex(centro + girar(glm::vec2(std::cos(a) * (rx + contorno), std::sin(a) * (ry + contorno)), giro));
            }
            ofVertex(centro + girar(glm::vec2(rx + contorno, contorno), giro));
            ofVertex(centro + girar(glm::vec2(-rx - contorno, contorno), giro));
            ofEndShape(true);
            for (int g = 0; g < GOMOS; ++g) {
                ofSetColor(g % 2 == 0 ? sal.velame1 : sal.velame2, alpha);
                ofBeginShape();
                ofVertex(centro);
                for (int p = 0; p <= PASSOS; ++p) {
                    const float a = PI + PI * (g + static_cast<float>(p) / PASSOS) / GOMOS;
                    ofVertex(centro + girar(glm::vec2(std::cos(a) * rx, std::sin(a) * ry), giro));
                }
                ofEndShape(true);
            }
            ofSetColor(CONTORNO, alpha);
            for (float lado : {-1.0f, -0.4f, 0.4f, 1.0f}) {
                const glm::vec2 de = centro + girar(glm::vec2(lado * rx * 0.97f, 2 * s), giro);
                const glm::vec2 ate = ponto({lado < 0 ? -26.0f : 26.0f, -36});
                capsula(de, ate, 1.0f * s, CONTORNO, alpha, 0);
            }
        }
    }

    // Corpo: pernas, macacão, braços, luvas e botas.
    const ofColor bota(60, 52, 70);
    capsula(ponto({-12, 20}), ponto(peE), 12 * s, sal.macacao, alpha, contorno);
    capsula(ponto({12, 20}), ponto(peD), 12 * s, sal.macacao, alpha, contorno);
    elipse(ponto(peE + glm::vec2(-4, 4)), 15 * s, 9 * s, bota, alpha, contorno);
    elipse(ponto(peD + glm::vec2(4, 4)), 15 * s, 9 * s, bota, alpha, contorno);
    capsula(ponto({0, -22}), ponto({0, 22}), 25 * s, sal.macacao, alpha, contorno);
    capsula(ponto({-18, -28}), ponto(maoE), 11 * s, sal.macacao, alpha, contorno);
    capsula(ponto({18, -28}), ponto(maoD), 11 * s, sal.macacao, alpha, contorno);
    elipse(ponto(maoE), 11 * s, 11 * s, ofColor(250, 250, 250), alpha, contorno);
    elipse(ponto(maoD), 11 * s, 11 * s, ofColor(250, 250, 250), alpha, contorno);
    // Faixa do equipamento no peito.
    ofSetColor(CONTORNO, alpha * 0.85f);
    ofDrawRectangle(ponto({-22, -6}).x, ponto({-22, -6}).y, 44 * s, 7 * s);

    // Cabeça: o rosto real da pessoa, recortado em círculo do quadro da câmera.
    const glm::vec2 cabeca = ponto({0, -72});
    const float raioCabeca = 54 * s;
    ofSetColor(CONTORNO, alpha);
    ofDrawCircle(cabeca, raioCabeca + 5 * s);
    ofSetColor(255, 255, 255, alpha);
    ofDrawCircle(cabeca, raioCabeca + 2.5f * s);

    const RostoCamera* rosto = nullptr;
    for (const RostoCamera& r : rostos) {
        if (r.textura != nullptr && r.presenca > 0.3f && (r.id == sal.rosto || rosto == nullptr)) {
            rosto = &r;
            if (r.id == sal.rosto) {
                break;
            }
        }
    }
    if (rosto != nullptr) {
        // Disco com o rosto já recortado (centro da textura = centro do rosto).
        constexpr float RAIO_TEXTURA = 0.43f;
        ofMesh disco;
        disco.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
        disco.addVertex({cabeca.x, cabeca.y, 0});
        disco.addTexCoord({0.5f, 0.5f});
        constexpr int LADOS = 48;
        for (int i = 0; i <= LADOS; ++i) {
            const float a = TWO_PI * i / LADOS;
            disco.addVertex({cabeca.x + std::cos(a) * raioCabeca, cabeca.y + std::sin(a) * raioCabeca, 0});
            disco.addTexCoord({0.5f + std::cos(a) * RAIO_TEXTURA, 0.5f + std::sin(a) * RAIO_TEXTURA});
        }
        ofSetColor(255, 255, 255, alpha);
        rosto->textura->bind();
        disco.draw();
        rosto->textura->unbind();
    } else {
        // Ninguém na câmera: carinha de desenho.
        ofSetColor(pele, alpha);
        ofDrawCircle(cabeca, raioCabeca);
        ofSetColor(CONTORNO, alpha);
        ofDrawCircle(cabeca + glm::vec2(-13, -6) * s, 5 * s);
        ofDrawCircle(cabeca + glm::vec2(13, -6) * s, 5 * s);
        ofDrawRectRounded(cabeca.x - 12 * s, cabeca.y + 12 * s, 24 * s, 5 * s, 2.5f * s);
    }
    // Capacete: aro colorido em volta do rosto.
    ofNoFill();
    ofSetLineWidth(1);
    ofFill();
    for (int i = 0; i < 28; ++i) {
        const float a0 = PI * 1.05f + PI * 0.9f * i / 28.0f;
        const glm::vec2 p = cabeca + glm::vec2(std::cos(a0), std::sin(a0)) * (raioCabeca + 1.5f * s);
        ofSetColor(sal.macacao, alpha);
        ofDrawCircle(p, 5.5f * s);
    }
}
