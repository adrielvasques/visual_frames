#include "PainelApp.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

const ofColor FUNDO(15, 16, 23);
const ofColor CARTAO(27, 29, 42);
const ofColor CARTAO_SOBRE(35, 38, 56);
const ofColor LINHA(46, 49, 70);
const ofColor TEXTO(236, 239, 250);
const ofColor SUAVE(139, 146, 172);
const ofColor TRILHA(46, 49, 70);
const ofColor VERDE(80, 220, 140);
const ofColor COR_LUZ(130, 112, 255);
const ofColor COR_BICHO(255, 128, 180);
const ofColor COR_ECOS(80, 210, 255);
const ofColor COR_NEON(120, 255, 170);
const ofColor COR_LEGO(255, 196, 40);

// Grade de medidas: tudo múltiplo de 2/4 px, alturas fixas por tipo de item.
constexpr float MARGEM = 20;
constexpr float ESPACO = 8;          // entre itens do mesmo grupo
constexpr float ANTES_SECAO = 18;    // respiro antes de cada seção
constexpr float SECAO = 22;          // altura do título de seção
constexpr float CABECALHO = 84;
constexpr float CARTAO_EFEITO = 76;
constexpr float LINHA_INTERRUPTOR = 54;
constexpr float LINHA_SLIDER = 62;
constexpr float CARTAO_ANIMAL = 104;
constexpr float RODAPE = 48;
constexpr float RAIO = 12;

float aproximar(float atual, float alvo, float dt, float tau) {
    return atual + (alvo - atual) * (1.0f - std::exp(-dt / tau));
}

std::string numero(float valor, int casas) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(casas) << valor;
    return s.str();
}

} // namespace

void PainelApp::setup() {
    ofSetVerticalSync(true);
    ofSetFrameRate(60);
    ofSetCircleResolution(64);
    ofBackground(FUNDO);

    auto carregar = [](ofTrueTypeFont& fonte, const std::string& arquivo, int tamanho) {
        ofTrueTypeFontSettings configuracao(arquivo, tamanho);
        configuracao.antialiased = true;
        configuracao.addRanges(ofAlphabet::Latin);
        if (!fonte.load(configuracao)) {
            ofLogWarning("Painel") << "Fonte não encontrada: " << arquivo << " (usando a padrão do OF)";
        }
    };
    carregar(fonteTitulo, "NotoSans-Bold.ttf", 19);
    carregar(fonteNegrito, "NotoSans-Bold.ttf", 14);
    carregar(fonteTexto, "NotoSans-Regular.ttf", 13);
    carregar(fontePequena, "NotoSans-Regular.ttf", 11);
}

void PainelApp::update() {
    const float dt = static_cast<float>(ofGetLastFrameTime());
    if (estado->efeito != efeitoAnterior) {
        efeitoAnterior = estado->efeito;
        destaqueAtivo = 0;
    }
    destaqueAtivo = aproximar(destaqueAtivo, 1.0f, dt, 0.12f);
    nivelGrave = aproximar(nivelGrave, estado->grave, dt, estado->grave > nivelGrave ? 0.03f : 0.15f);
    nivelMedio = aproximar(nivelMedio, estado->medio, dt, estado->medio > nivelMedio ? 0.03f : 0.15f);
    nivelAgudo = aproximar(nivelAgudo, estado->agudo, dt, estado->agudo > nivelAgudo ? 0.03f : 0.15f);
    rolagem = std::clamp(rolagem, 0.0f, std::max(0.0f, alturaConteudo - areaVisivel()));
    if (estado->encerrar) {
        ofExit();
    }
}

float PainelApp::areaVisivel() const {
    return ofGetHeight() - RODAPE;
}

ofColor PainelApp::acento() const {
    // Cor do efeito ativo.
    switch (estado->efeito) {
    case Efeito::Luz: return COR_LUZ;
    case Efeito::Bichinho: return COR_BICHO;
    case Efeito::Ecos: return COR_ECOS;
    case Efeito::Paraquedas: return COR_NEON;
    case Efeito::Lego: return COR_LEGO;
    }
    return COR_LUZ;
}

void PainelApp::escrever(ofTrueTypeFont& fonte, const std::string& texto, float x, float y, const ofColor& cor) {
    ofSetColor(cor);
    fonte.drawString(texto, x, y);
}

void PainelApp::escreverCentralizado(
    ofTrueTypeFont& fonte, const std::string& texto, const ofRectangle& area, const ofColor& cor
) {
    const ofRectangle caixa = fonte.getStringBoundingBox(texto, 0, 0);
    escrever(fonte, texto, area.getCenter().x - caixa.width / 2 - caixa.x,
             area.getCenter().y - caixa.height / 2 - caixa.y, cor);
}

// Corta o texto com "…" para nunca invadir o controle ao lado.
std::string PainelApp::caber(ofTrueTypeFont& fonte, const std::string& texto, float largura) {
    if (fonte.stringWidth(texto) <= largura) {
        return texto;
    }
    std::string cortado = texto;
    while (!cortado.empty() && fonte.stringWidth(cortado + "…") > largura) {
        // Remove um caractere UTF-8 inteiro.
        do {
            cortado.pop_back();
        } while (!cortado.empty() && (static_cast<unsigned char>(cortado.back()) & 0xC0) == 0x80);
        if (!cortado.empty() && (static_cast<unsigned char>(cortado.back()) & 0xC0) == 0xC0) {
            cortado.pop_back();
        }
    }
    return cortado + "…";
}

float PainelApp::larguraUtil() const {
    return ofGetWidth() - 2 * MARGEM;
}

void PainelApp::draw() {
    alvos.clear();
    ofClear(FUNDO);
    ofEnableAlphaBlending();

    // Conteúdo rola; o rodapé fica fixo.
    ofPushMatrix();
    ofTranslate(0, -rolagem);
    float y = cabecalho(0);

    y = secao("Efeito", y);
    y = cartaoEfeito(y, Efeito::Luz, "Luz reativa", "Pontos neon no contorno, com cores que dançam com a música.");
    y = cartaoEfeito(y, Efeito::Bichinho, "Bichinho espelho", "A criança vira um personagem que imita cada movimento.");
    y = cartaoEfeito(y, Efeito::Ecos, "Ecos em quadrinhos", "A pessoa vira HQ e deixa ecos coloridos para trás.");
    y = cartaoEfeito(y, Efeito::Paraquedas, "Salto de paraquedas", "Um avião solta paraquedistas com o rosto de quem está na frente.");
    y = cartaoEfeito(y, Efeito::Lego, "LEGO", "A pessoa vira um mosaico de peças; ao se mexer, as peças caem.");

    if (estado->efeito == Efeito::Luz) {
        y = secao("Ajustes da luz", y);
        y = interruptor(y, "Orbes nas mãos", "Brilho que cresce com o movimento", estado->orbesMaos);
        y = interruptor(y, "Cor segue a música", "Desligue para fixar uma cor", estado->corSegueMusica);
        y = controleDeslizante(
            y, "Cor", estado->matizFixo, 0.0f, 1.0f,
            estado->corSegueMusica ? "automática" : numero(estado->matizFixo * 360.0f, 0) + "°",
            true, estado->corSegueMusica
        );
        y = controleDeslizante(
            y, "Sensibilidade do som", estado->sensibilidadeSom, 0.2f, 3.0f, numero(estado->sensibilidadeSom, 1) + "×"
        );
        y = controleDeslizante(y, "Brilho", estado->brilho, 0.3f, 1.6f, numero(estado->brilho * 100.0f, 0) + "%");
        y = secao("Som ao vivo", y);
        y = medidores(y);
    } else if (estado->efeito == Efeito::Lego) {
        y = secao("Ajustes do LEGO", y);
        y = controleDeslizante(
            y, "Tamanho das peças", estado->tamanhoPeca, 10.0f, 28.0f, numero(std::round(estado->tamanhoPeca), 0) + " px"
        );
        y = interruptor(y, "Peças caem ao se mexer", "As peças de onde você saiu se soltam", estado->legoCair);
        y = controleDeslizante(y, "Brilho", estado->brilho, 0.3f, 1.6f, numero(estado->brilho * 100.0f, 0) + "%");
        escrever(fontePequena, caber(fontePequena, "Dica: roupas coloridas viram peças coloridas.", larguraUtil()), MARGEM, y + 14, SUAVE);
        y += 24;
    } else if (estado->efeito == Efeito::Paraquedas) {
        y = secao("Ajustes do salto", y);
        y = controleDeslizante(
            y, "Tempo até a mesma pessoa saltar de novo", estado->intervaloAviao, 3.0f, 20.0f, numero(std::round(estado->intervaloAviao), 0) + " s"
        );
        escrever(fontePequena, caber(fontePequena, "O avião vem quando alguém olha para a câmera.", larguraUtil()), MARGEM, y + 14, SUAVE);
        escrever(fontePequena, caber(fontePequena, "Dica: faça careta, o paraquedista faz junto!", larguraUtil()), MARGEM, y + 31, SUAVE);
        y += 42;
    } else if (estado->efeito == Efeito::Ecos) {
        y = secao("Ajustes dos ecos", y);
        y = controleDeslizante(
            y, "Quantidade de ecos", estado->quantidadeEcos, 1.0f, 8.0f, numero(std::round(estado->quantidadeEcos), 0)
        );
        y = controleDeslizante(
            y, "Distância entre ecos", estado->distanciaEcos, 1.0f, 8.0f,
            numero(std::round(estado->distanciaEcos) * 33.0f, 0) + " ms"
        );
        y = interruptor(y, "Cor segue a música", "Desligue para fixar uma cor", estado->corSegueMusica);
        y = controleDeslizante(
            y, "Cor", estado->matizFixo, 0.0f, 1.0f,
            estado->corSegueMusica ? "automática" : numero(estado->matizFixo * 360.0f, 0) + "°",
            true, estado->corSegueMusica
        );
        y = controleDeslizante(
            y, "Sensibilidade do som", estado->sensibilidadeSom, 0.2f, 3.0f, numero(estado->sensibilidadeSom, 1) + "×"
        );
        y = controleDeslizante(y, "Brilho", estado->brilho, 0.3f, 1.6f, numero(estado->brilho * 100.0f, 0) + "%");
        escrever(fontePequena, caber(fontePequena, "Dica: dance rápido para abrir um leque de ecos.", larguraUtil()), MARGEM, y + 14, SUAVE);
        y += 24;
        y = secao("Som ao vivo", y);
        y = medidores(y);
    } else {
        y = secao("Escolha o bichinho", y);
        const float largura = (larguraUtil() - 2 * ESPACO) / 3.0f;
        const char* nomes[3] = {"Gato", "Dinossauro", "Coelho"};
        for (int i = 0; i < 3; ++i) {
            botaoAnimal(ofRectangle(MARGEM + i * (largura + ESPACO), y, largura, CARTAO_ANIMAL), static_cast<Animal>(i), nomes[i]);
        }
        y += CARTAO_ANIMAL + ESPACO;
        y = secao("Ajustes do bichinho", y);
        y = interruptor(y, "Fundo colorido", "Desligue para fundo preto", estado->fundoColorido);
        y = controleDeslizante(y, "Brilho", estado->brilho, 0.3f, 1.6f, numero(estado->brilho * 100.0f, 0) + "%");
        escrever(fontePequena, caber(fontePequena, "Dica: mexa a cabeça e as orelhas balançam.", larguraUtil()), MARGEM, y + 14, SUAVE);
        y += 24;
    }
    alturaConteudo = y + MARGEM;
    ofPopMatrix();

    // Barra de rolagem fina quando o conteúdo não cabe.
    const float visivel = areaVisivel();
    if (alturaConteudo > visivel) {
        const float altura = std::max(30.0f, visivel * visivel / alturaConteudo);
        const float topo = (visivel - altura) * rolagem / (alturaConteudo - visivel);
        ofSetColor(SUAVE, 120);
        ofDrawRectRounded(ofGetWidth() - 6, topo + 4, 3, altura - 8, 1.5f);
    }

    // Rodapé fixo
    ofSetColor(FUNDO);
    ofDrawRectangle(0, visivel, ofGetWidth(), RODAPE);
    ofSetColor(LINHA);
    ofDrawRectangle(0, visivel, ofGetWidth(), 1);
    escrever(fontePequena, caber(fontePequena, "Teclas 1 a 5 trocam o efeito · d debug · f tela cheia", larguraUtil()),
             MARGEM, visivel + 29, SUAVE);
}

float PainelApp::cabecalho(float y) {
    ofSetColor(acento());
    ofDrawRectangle(0, y, ofGetWidth(), 4);
    escrever(fonteTitulo, "Controle da projeção", MARGEM, y + 40, TEXTO);

    const bool visaoOk = estado->visaoMs > 0;
    ofSetColor(visaoOk ? VERDE : ofColor(240, 180, 60));
    ofDrawCircle(MARGEM + 4, y + 61, 4);
    std::ostringstream linha;
    if (visaoOk) {
        linha << "Câmera ligada · " << estado->pessoas << (estado->pessoas == 1 ? " pessoa" : " pessoas") << " · "
              << numero(estado->visaoMs, 0) << " ms " << (estado->usandoCuda ? "(GPU)" : "(CPU)");
    } else {
        linha << "Aguardando a câmera…";
    }
    escrever(fontePequena, caber(fontePequena, linha.str(), larguraUtil() - 16), MARGEM + 16, y + 65, SUAVE);
    return y + CABECALHO;
}

float PainelApp::secao(const std::string& titulo, float y) {
    y += ANTES_SECAO - ESPACO;
    std::string maiusculas = ofToUpper(titulo);
    escrever(fontePequena, maiusculas, MARGEM, y + 12, SUAVE);
    const float largura = fontePequena.stringWidth(maiusculas);
    ofSetColor(LINHA);
    ofDrawRectangle(MARGEM + largura + 10, y + 8, larguraUtil() - largura - 10, 1);
    return y + SECAO;
}

void PainelApp::desenharIcone(Icone icone, const ofRectangle& area, const ofColor& cor, float pulso) {
    const glm::vec2 c = area.getCenter();
    const float r = std::min(area.width, area.height) / 2;
    ofSetColor(cor);
    switch (icone) {
    case Icone::Luz: {
        // Anel de pontos que pulsa, como o contorno da projeção.
        for (int i = 0; i < 14; ++i) {
            const float a = i * TWO_PI / 14 + pulso * 0.6f;
            const float raio = r * (0.72f + 0.06f * std::sin(pulso * 3.0f + i));
            ofSetColor(cor, 150 + 100 * (i % 2));
            ofDrawCircle(c.x + std::cos(a) * raio, c.y + std::sin(a) * raio, r * (0.07f + 0.03f * (i % 3)));
        }
        for (int i = 0; i < 6; ++i) {
            const float a = i * 2.4f;
            ofSetColor(cor, 110);
            ofDrawCircle(c.x + std::cos(a) * r * 0.3f * (i % 3 + 1) / 2, c.y + std::sin(a) * r * 0.3f * (i % 3 + 1) / 2, r * 0.05f);
        }
        break;
    }
    case Icone::Ecos: {
        // Estrela de explosão de HQ com dois ecos de cor atrás.
        auto estrela = [&](glm::vec2 centro, float raio, const ofColor& corEstrela) {
            ofPath caminho;
            for (int i = 0; i < 20; ++i) {
                const float a = i * PI / 10 + pulso * 0.3f;
                const float rr = i % 2 == 0 ? raio : raio * 0.62f;
                if (i == 0) {
                    caminho.moveTo(centro.x + std::cos(a) * rr, centro.y + std::sin(a) * rr);
                } else {
                    caminho.lineTo(centro.x + std::cos(a) * rr, centro.y + std::sin(a) * rr);
                }
            }
            caminho.close();
            caminho.setFillColor(corEstrela);
            caminho.draw();
        };
        estrela(c + glm::vec2(-r * 0.22f, r * 0.10f), r * 0.85f, ofColor(255, 90, 170, 160));
        estrela(c + glm::vec2(-r * 0.10f, r * 0.05f), r * 0.85f, ofColor(cor, 190));
        estrela(c, r * 0.85f, ofColor(255, 226, 60));
        ofSetColor(16, 12, 26);
        ofDrawRectangle(c.x - r * 0.07f, c.y - r * 0.38f, r * 0.14f, r * 0.48f);
        ofDrawCircle(c.x, c.y + r * 0.30f, r * 0.08f);
        break;
    }
    case Icone::Neon: {
        // Paraquedas listrado com um bonequinho pendurado.
        const glm::vec2 cupula(c.x, c.y - r * 0.35f);
        for (int g = 0; g < 4; ++g) {
            ofSetColor(g % 2 == 0 ? cor : ofColor(255, 120, 220));
            ofBeginShape();
            ofVertex(cupula);
            for (int s = 0; s <= 6; ++s) {
                const float a = PI + PI * (g + s / 6.0f) / 4.0f;
                ofVertex(cupula.x + std::cos(a) * r * 0.85f, cupula.y + std::sin(a) * r * 0.55f);
            }
            ofEndShape(true);
        }
        ofSetColor(cor);
        ofSetLineWidth(1.5f);
        ofDrawLine(cupula.x - r * 0.85f, cupula.y, c.x - r * 0.12f, c.y + r * 0.25f);
        ofDrawLine(cupula.x + r * 0.85f, cupula.y, c.x + r * 0.12f, c.y + r * 0.25f);
        ofDrawCircle(c.x, c.y + r * 0.22f, r * 0.12f);
        ofDrawRectangle(c.x - r * 0.04f, c.y + r * 0.32f, r * 0.08f, r * 0.35f);
        ofDrawLine(c.x, c.y + r * 0.67f, c.x - r * 0.15f, c.y + r * 0.92f);
        ofDrawLine(c.x, c.y + r * 0.67f, c.x + r * 0.15f, c.y + r * 0.92f);
        break;
    }
    case Icone::Lego: {
        // Peça 2x2 com pinos.
        const ofRectangle peca(c.x - r * 0.8f, c.y - r * 0.5f, r * 1.6f, r * 1.1f);
        ofSetColor(cor);
        ofDrawRectRounded(peca, r * 0.08f);
        ofSetColor(cor.r * 0.75f, cor.g * 0.75f, cor.b * 0.75f);
        ofDrawRectangle(peca.x, peca.getBottom() - r * 0.18f, peca.width, r * 0.18f);
        for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
                const glm::vec2 pino(c.x + (i - 0.5f) * r * 0.75f, c.y - r * 0.18f + (j - 0.5f) * r * 0.5f);
                ofSetColor(cor.r * 0.7f, cor.g * 0.7f, cor.b * 0.7f);
                ofDrawCircle(pino + glm::vec2(0, r * 0.05f), r * 0.17f);
                ofSetColor(std::min(255, cor.r + 30), std::min(255, cor.g + 30), std::min(255, cor.b + 30));
                ofDrawCircle(pino, r * 0.17f);
            }
        }
        break;
    }
    case Icone::Bichinho:
    case Icone::Gato: {
        // Cabeça de gato: duas orelhas, focinho e bigodes.
        ofSetColor(cor);
        ofDrawTriangle(c.x - r * 0.75f, c.y - r * 0.05f, c.x - r * 0.15f, c.y - r * 0.55f, c.x - r * 0.62f, c.y - r * 0.95f);
        ofDrawTriangle(c.x + r * 0.75f, c.y - r * 0.05f, c.x + r * 0.15f, c.y - r * 0.55f, c.x + r * 0.62f, c.y - r * 0.95f);
        ofSetColor(255, 140, 190);
        ofDrawTriangle(c.x - r * 0.14f, c.y + r * 0.15f, c.x + r * 0.14f, c.y + r * 0.15f, c.x, c.y + r * 0.32f);
        ofSetColor(cor, 200);
        ofSetLineWidth(1.6f);
        for (int s = -1; s <= 1; s += 2) {
            for (int k = -1; k <= 1; ++k) {
                ofDrawLine(c.x + s * r * 0.25f, c.y + r * 0.2f, c.x + s * r * 0.85f, c.y + r * 0.2f + k * r * 0.16f);
            }
        }
        break;
    }
    case Icone::Dinossauro: {
        // Três chifres e focinho.
        ofSetColor(cor);
        ofDrawTriangle(c.x - r * 0.18f, c.y - r * 0.2f, c.x + r * 0.18f, c.y - r * 0.2f, c.x, c.y - r * 0.95f);
        ofDrawTriangle(c.x - r * 0.75f, c.y - r * 0.05f, c.x - r * 0.3f, c.y - r * 0.2f, c.x - r * 0.72f, c.y - r * 0.7f);
        ofDrawTriangle(c.x + r * 0.75f, c.y - r * 0.05f, c.x + r * 0.3f, c.y - r * 0.2f, c.x + r * 0.72f, c.y - r * 0.7f);
        ofSetColor(120, 230, 140);
        ofDrawEllipse(c.x, c.y + r * 0.3f, r * 0.8f, r * 0.5f);
        ofSetColor(FUNDO);
        ofDrawCircle(c.x - r * 0.12f, c.y + r * 0.28f, r * 0.05f);
        ofDrawCircle(c.x + r * 0.12f, c.y + r * 0.28f, r * 0.05f);
        break;
    }
    case Icone::Coelho: {
        // Orelhas compridas, nariz e dentinhos.
        ofSetColor(cor);
        ofDrawEllipse(c.x - r * 0.3f, c.y - r * 0.5f, r * 0.32f, r * 0.95f);
        ofDrawEllipse(c.x + r * 0.3f, c.y - r * 0.5f, r * 0.32f, r * 0.95f);
        ofSetColor(255, 140, 190);
        ofDrawEllipse(c.x - r * 0.3f, c.y - r * 0.5f, r * 0.14f, r * 0.6f);
        ofDrawEllipse(c.x + r * 0.3f, c.y - r * 0.5f, r * 0.14f, r * 0.6f);
        ofDrawCircle(c.x, c.y + r * 0.2f, r * 0.08f);
        ofSetColor(255);
        ofDrawRectangle(c.x - r * 0.09f, c.y + r * 0.3f, r * 0.08f, r * 0.2f);
        ofDrawRectangle(c.x + r * 0.01f, c.y + r * 0.3f, r * 0.08f, r * 0.2f);
        break;
    }
    }
}

float PainelApp::cartaoEfeito(float y, Efeito efeito, const std::string& titulo, const std::string& descricao) {
    const ofRectangle area(MARGEM, y, larguraUtil(), CARTAO_EFEITO);
    const float destaque = efeito == estado->efeito ? destaqueAtivo : 0.0f;
    const bool ativo = estado->efeito == efeito;
    const ofColor cor = efeito == Efeito::Luz ? COR_LUZ
        : efeito == Efeito::Bichinho          ? COR_BICHO
        : efeito == Efeito::Ecos              ? COR_ECOS
        : efeito == Efeito::Paraquedas        ? COR_NEON
                                              : COR_LEGO;

    ofSetColor((sobMouse(area) ? CARTAO_SOBRE : CARTAO).getLerped(ofColor(cor.r * 0.28f, cor.g * 0.28f, cor.b * 0.3f), destaque * 0.8f));
    ofDrawRectRounded(area, RAIO);
    ofNoFill();
    ofSetLineWidth(2.0f);
    ofSetColor(LINHA.getLerped(cor, destaque));
    ofDrawRectRounded(area, RAIO);
    ofFill();

    constexpr float ICONE = 48;
    desenharIcone(
        efeito == Efeito::Luz ? Icone::Luz
            : efeito == Efeito::Bichinho   ? Icone::Bichinho
            : efeito == Efeito::Ecos       ? Icone::Ecos
            : efeito == Efeito::Paraquedas ? Icone::Neon
                                           : Icone::Lego,
        ofRectangle(area.x + 14, area.y + (area.height - ICONE) / 2, ICONE, ICONE), cor, ofGetElapsedTimef()
    );
    const float textoX = area.x + 14 + ICONE + 14;
    const float larguraTexto = area.getRight() - 14 - textoX;

    // Selo "ATIVO" à direita do título.
    float larguraTitulo = larguraTexto;
    if (ativo) {
        const ofRectangle selo(area.getRight() - 14 - 52, area.y + 12, 52, 18);
        ofSetColor(cor);
        ofDrawRectRounded(selo, 9);
        escreverCentralizado(fontePequena, "ATIVO", selo, FUNDO);
        larguraTitulo -= 60;
    }
    escrever(fonteNegrito, caber(fonteNegrito, titulo, larguraTitulo), textoX, area.y + 27, TEXTO);

    // Descrição em até duas linhas na largura disponível.
    std::string linha1;
    std::string linha2;
    std::istringstream palavras(descricao);
    std::string palavra;
    while (palavras >> palavra) {
        const bool cabeNaPrimeira = linha2.empty() && fontePequena.stringWidth(linha1 + " " + palavra) < larguraTexto;
        std::string& destino = cabeNaPrimeira ? linha1 : linha2;
        destino += (destino.empty() ? "" : " ") + palavra;
    }
    escrever(fontePequena, linha1, textoX, area.y + 47, SUAVE);
    escrever(fontePequena, caber(fontePequena, linha2, larguraTexto), textoX, area.y + 62, SUAVE);

    alvos.push_back({area, [this, efeito](float, float) { estado->efeito = efeito; }, nullptr});
    return y + CARTAO_EFEITO + ESPACO;
}

void PainelApp::botaoAnimal(const ofRectangle& area, Animal animal, const std::string& nome) {
    const bool ativo = estado->animal == animal;
    ofSetColor(ativo ? ofColor(COR_BICHO.r * 0.3f, COR_BICHO.g * 0.3f, COR_BICHO.b * 0.34f) : (sobMouse(area) ? CARTAO_SOBRE : CARTAO));
    ofDrawRectRounded(area, RAIO);
    ofNoFill();
    ofSetLineWidth(2.0f);
    ofSetColor(ativo ? COR_BICHO : LINHA);
    ofDrawRectRounded(area, RAIO);
    ofFill();
    const Icone icones[3] = {Icone::Gato, Icone::Dinossauro, Icone::Coelho};
    const float icone = std::min(56.0f, area.width - 24);
    desenharIcone(
        icones[static_cast<int>(animal)],
        ofRectangle(area.getCenter().x - icone / 2, area.y + 14, icone, icone), ativo ? COR_BICHO : ofColor(190, 195, 220), 0
    );
    escreverCentralizado(
        fonteTexto, caber(fonteTexto, nome, area.width - 12), ofRectangle(area.x, area.getBottom() - 30, area.width, 20),
        ativo ? TEXTO : SUAVE
    );
    alvos.push_back({area, [this, animal](float, float) { estado->animal = animal; }, nullptr});
}

float PainelApp::interruptor(float y, const std::string& rotulo, const std::string& dica, bool& valor) {
    const ofRectangle area(MARGEM, y, larguraUtil(), LINHA_INTERRUPTOR);
    ofSetColor(sobMouse(area) ? CARTAO_SOBRE : CARTAO);
    ofDrawRectRounded(area, RAIO);

    const ofRectangle trilha(area.getRight() - 14 - 40, area.y + (area.height - 22) / 2, 40, 22);
    const float larguraTexto = trilha.x - 12 - (area.x + 14);
    escrever(fonteTexto, caber(fonteTexto, rotulo, larguraTexto), area.x + 14, area.y + 23, TEXTO);
    escrever(fontePequena, caber(fontePequena, dica, larguraTexto), area.x + 14, area.y + 41, SUAVE);

    ofSetColor(valor ? acento() : ofColor(70, 74, 98));
    ofDrawRectRounded(trilha, 11);
    ofSetColor(255);
    ofDrawCircle(trilha.x + (valor ? trilha.width - 11 : 11), trilha.y + 11, 8);

    bool* alvo = &valor;
    alvos.push_back({area, [alvo](float, float) { *alvo = !*alvo; }, nullptr});
    return y + LINHA_INTERRUPTOR + ESPACO;
}

float PainelApp::controleDeslizante(
    float y, const std::string& rotulo, float& valor, float minimo, float maximo, const std::string& texto,
    bool arcoIris, bool desativado
) {
    const ofRectangle area(MARGEM, y, larguraUtil(), LINHA_SLIDER);
    ofSetColor(CARTAO);
    ofDrawRectRounded(area, RAIO);
    const float larguraValor = fontePequena.stringWidth(texto);
    escrever(fonteTexto, caber(fonteTexto, rotulo, area.width - 28 - larguraValor - 12), area.x + 14, area.y + 24,
             desativado ? SUAVE : TEXTO);
    escrever(fontePequena, texto, area.getRight() - 14 - larguraValor, area.y + 24, SUAVE);

    const ofRectangle trilha(area.x + 16, area.y + 40, area.width - 32, 6);
    const float t = ofMap(valor, minimo, maximo, 0.0f, 1.0f, true);
    if (arcoIris) {
        constexpr int PASSOS = 48;
        for (int i = 0; i < PASSOS; ++i) {
            ofSetColor(ofColor::fromHsb(255.0f * i / PASSOS, desativado ? 70 : 200, desativado ? 120 : 255));
            ofDrawRectangle(trilha.x + trilha.width * i / PASSOS, trilha.y, trilha.width / PASSOS + 1, trilha.height);
        }
    } else {
        ofSetColor(TRILHA);
        ofDrawRectRounded(trilha, 3);
        ofSetColor(acento());
        ofDrawRectRounded(trilha.x, trilha.y, std::max(6.0f, trilha.width * t), trilha.height, 3);
    }
    if (!desativado) {
        ofSetColor(0, 0, 0, 70);
        ofDrawCircle(trilha.x + trilha.width * t, trilha.y + 4, 9);
        ofSetColor(255);
        ofDrawCircle(trilha.x + trilha.width * t, trilha.y + 3, 8);
        float* alvo = &valor;
        auto definir = [alvo, trilha, minimo, maximo](float x, float) {
            *alvo = ofMap(x, trilha.x, trilha.x + trilha.width, minimo, maximo, true);
        };
        alvos.push_back({ofRectangle(area.x, area.y + 28, area.width, area.height - 28), definir, definir});
    }
    return y + LINHA_SLIDER + ESPACO;
}

float PainelApp::medidores(float y) {
    constexpr float LINHA = 22;
    const ofRectangle area(MARGEM, y, larguraUtil(), 14 + 3 * LINHA + 10);
    ofSetColor(CARTAO);
    ofDrawRectRounded(area, RAIO);
    const char* nomes[3] = {"Grave", "Médio", "Agudo"};
    const float niveis[3] = {nivelGrave, nivelMedio, nivelAgudo};
    const ofColor cores[3] = {ofColor(255, 90, 140), ofColor(255, 200, 80), ofColor(90, 210, 255)};
    const float inicioBarra = area.x + 14 + fontePequena.stringWidth("Agudo") + 14;
    for (int i = 0; i < 3; ++i) {
        const float centro = area.y + 14 + i * LINHA + LINHA / 2 - 4;
        escrever(fontePequena, nomes[i], area.x + 14, centro + 4, SUAVE);
        const ofRectangle trilha(inicioBarra, centro - 3, area.getRight() - 14 - inicioBarra, 6);
        ofSetColor(TRILHA);
        ofDrawRectRounded(trilha, 3);
        ofSetColor(estado->audioAtivo ? cores[i] : ofColor(90, 94, 118));
        ofDrawRectRounded(trilha.x, trilha.y, std::max(6.0f, trilha.width * niveis[i]), trilha.height, 3);
    }
    if (!estado->audioAtivo) {
        const std::string aviso = "sem música tocando";
        escrever(fontePequena, aviso, area.getRight() - 14 - fontePequena.stringWidth(aviso), area.y + 14, SUAVE);
    }
    return y + area.height + ESPACO;
}

void PainelApp::mousePressed(int x, int y, int) {
    mouse = {x, y};
    pressionado = ofRectangle();
    // O X11/gerenciador de janelas às vezes entrega um clique fantasma quando a
    // janela abre; nada é aceito no primeiro segundo. Rodapé não é clicável.
    if (ofGetElapsedTimef() < 1.0f || y >= areaVisivel()) {
        return;
    }
    const float yConteudo = y + rolagem;
    for (int i = static_cast<int>(alvos.size()) - 1; i >= 0; --i) {
        if (!alvos[i].area.inside(x, yConteudo)) {
            continue;
        }
        if (alvos[i].aoArrastar) {
            // Slider: responde já no clique e enquanto arrasta.
            alvos[i].aoArrastar(x, yConteudo);
            arrastando = i;
        } else {
            pressionado = alvos[i].area;
        }
        return;
    }
}

void PainelApp::mouseDragged(int x, int y, int) {
    mouse = {x, y};
    if (arrastando >= 0 && arrastando < static_cast<int>(alvos.size()) && alvos[arrastando].aoArrastar) {
        alvos[arrastando].aoArrastar(x, y + rolagem);
    }
}

void PainelApp::mouseReleased(int x, int y, int) {
    arrastando = -1;
    const ofRectangle area = pressionado;
    pressionado = ofRectangle();
    // Botões e interruptores disparam ao soltar dentro da mesma área (como em
    // qualquer interface). Eventos repetidos em menos de 150 ms são ignorados,
    // senão um clique duplicado liga e desliga o interruptor na hora.
    const float agora = ofGetElapsedTimef();
    const float yConteudo = y + rolagem;
    if (area.isEmpty() || !area.inside(x, yConteudo) || agora - ultimoDisparo < 0.15f) {
        return;
    }
    for (const auto& alvo : alvos) {
        if (alvo.area == area && alvo.aoPressionar) {
            alvo.aoPressionar(x, yConteudo);
            ultimoDisparo = agora;
            return;
        }
    }
}

void PainelApp::mouseScrolled(int, int, float, float scrollY) {
    rolagem = std::clamp(rolagem - scrollY * 40.0f, 0.0f, std::max(0.0f, alturaConteudo - areaVisivel()));
}

void PainelApp::mouseMoved(int x, int y) {
    mouse = {x, y};
}

void PainelApp::keyPressed(int key) {
    if (key == 'q' || key == 'Q' || key == OF_KEY_ESC) {
        ofExit();
    } else if (key == '1') {
        estado->efeito = Efeito::Luz;
    } else if (key == '2') {
        estado->efeito = Efeito::Bichinho;
    } else if (key == '3') {
        estado->efeito = Efeito::Ecos;
    } else if (key == '4') {
        estado->efeito = Efeito::Paraquedas;
    } else if (key == '5') {
        estado->efeito = Efeito::Lego;
    }
}
