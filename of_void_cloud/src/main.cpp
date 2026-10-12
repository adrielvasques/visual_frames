#include "ofMain.h"
#include "ofApp.h"
#include "PainelApp.h"

#include <cstdlib>

int main() {
    auto estado = std::make_shared<EstadoPainel>();
    // VISUAL_FRAMES_EFEITO=bichinho|ecos|paraquedas|lego abre direto nesse efeito.
    if (const char* efeito = std::getenv("VISUAL_FRAMES_EFEITO")) {
        const std::string nome(efeito);
        if (nome == "bichinho") {
            estado->efeito = Efeito::Bichinho;
        } else if (nome == "ecos") {
            estado->efeito = Efeito::Ecos;
        } else if (nome == "paraquedas") {
            estado->efeito = Efeito::Paraquedas;
        } else if (nome == "lego") {
            estado->efeito = Efeito::Lego;
        }
    }

    ofGLFWWindowSettings projecao;
    projecao.setGLVersion(3, 2);
    projecao.setSize(1280, 720);
    projecao.setPosition({460, 60});
    projecao.windowMode = OF_WINDOW;
    projecao.title = "Projecao";

    // VISUAL_FRAMES_MONITOR=<n> abre a projeção em tela cheia no monitor n
    // (ordem do GLFW: 0 costuma ser a tela principal, 1 o projetor no HDMI).
    if (const char* monitor = std::getenv("VISUAL_FRAMES_MONITOR")) {
        projecao.monitor = std::atoi(monitor);
        projecao.windowMode = OF_FULLSCREEN;
    }
    auto janelaProjecao = ofCreateWindow(projecao);

    // O painel de controle é uma janela à parte, que nunca vai para o projetor.
    ofGLFWWindowSettings painel;
    painel.setGLVersion(3, 2);
    painel.setSize(400, 760);
    painel.resizable = true;
    painel.setPosition({40, 60});
    painel.windowMode = OF_WINDOW;
    painel.title = "Painel de controle";
    painel.shareContextWith = janelaProjecao;
    auto janelaPainel = ofCreateWindow(painel);

    ofRunApp(janelaProjecao, std::make_shared<ofApp>(estado));
    ofRunApp(janelaPainel, std::make_shared<PainelApp>(estado));
    ofRunMainLoop();
}
