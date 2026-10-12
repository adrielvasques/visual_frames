#include "ofApp.h"

#include <cmath>
#include <iomanip>
#include <sstream>

#include <opencv2/imgproc.hpp>

namespace {

const std::string VERTEX_SHADER = R"(
#version 150
uniform mat4 modelViewProjectionMatrix;
in vec4 position;
in vec2 texcoord;
out vec2 uv;
void main() {
    uv = texcoord;
    gl_Position = modelViewProjectionMatrix * position;
}
)";

// Pontos em uv [0,1] (mesma orientação da máscara); pessoas[i] =
// (presença, energia pulso E, energia pulso D, altura do corpo em fração da tela).
// musica = (grave, médio, agudo, flash da batida), todos 0..1; matiz avança
// com a música e salta a cada batida.
const std::string FRAGMENT_SHADER = R"(
#version 150
#define MAX_PESSOAS 6
#define NUM_PONTOS 17
uniform sampler2D mascara;
uniform vec2 resolucao;
uniform float tempo;
uniform vec3 pontos[MAX_PESSOAS * NUM_PONTOS];
uniform vec4 pessoas[MAX_PESSOAS];
uniform float efeitoMaos;
uniform float efeitoNuvem;
uniform float efeitoEcos;
uniform float efeitoLego;
uniform float tamanhoPeca;   // lado do pino (px a 720p)
uniform float legoCair;      // 1 = peças caem quando a pessoa sai do lugar
uniform sampler2D camera;    // quadro da câmera (cores das peças)
uniform float ganhoCamera;   // compensa ambiente escuro (brilho médio da pessoa -> meio-tom)
uniform sampler2D ecosA;      // ecos 1-4 (um por canal), cada vez mais antigos
uniform sampler2D ecosB;      // ecos 5-8
uniform sampler2D carimbo;    // silhueta "carimbada" na última batida
uniform sampler2D quadrinho;  // pessoa em estilo HQ: RGB chapado + A = traço XDoG
uniform vec2 centroCena;      // centro da pessoa principal (uv), para os raios do fundo
uniform float numEcos;
uniform float carimboForca;
uniform float efeitoBichinho;
uniform float efeitoFundo;
uniform float animal;      // 0 gato, 1 dinossauro, 2 coelho
uniform float brilhoGeral;
uniform vec4 cabeca[MAX_PESSOAS]; // cx, cy (uv), raio (fração da altura da tela), validade
uniform vec4 corpo[MAX_PESSOAS];  // dirX, dirY (eixo dos olhos na tela), escala (agachar), mola das orelhas
uniform vec4 extra[MAX_PESSOAS];  // pupila x, pupila y, piscar (0..1), balanço do rabo
uniform vec4 rig1[MAX_PESSOAS];   // raiz x, y (uv), U (fração da altura da tela), validade
uniform vec4 rig2[MAX_PESSOAS];   // ângulo do corpo, ângulo da cabeça, ângulo perna esq., perna dir. (tela)
uniform vec4 bracos[MAX_PESSOAS]; // mão esq. (x, y), mão dir. (x, y): em U, a partir do ombro do boneco
uniform vec4 rig3[MAX_PESSOAS];   // esticar (squash & stretch), deslocamento da cabeça (x, y em U)
#define NUM_ELOS_RABO 8
uniform vec2 raboPontos[MAX_PESSOAS * NUM_ELOS_RABO]; // corrente de Verlet do rabo (uv)
uniform vec4 musica;
uniform float matiz;
in vec2 uv;
out vec4 corFinal;

// Cor neon: matiz livre, saturação alta, valor cheio.
vec3 neon(float h) {
    vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return mix(vec3(1.0), k, 0.88);
}

// Cada pessoa fica um pouco deslocada no círculo de cores.
vec3 corPessoa(int i) {
    return neon(matiz + 0.13 * float(i));
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float ruido(vec2 p) {
    vec2 inteiro = floor(p);
    vec2 fracao = fract(p);
    fracao = fracao * fracao * (3.0 - 2.0 * fracao);
    float a = hash(inteiro);
    float b = hash(inteiro + vec2(1.0, 0.0));
    float c = hash(inteiro + vec2(0.0, 1.0));
    float d = hash(inteiro + vec2(1.0, 1.0));
    return mix(mix(a, b, fracao.x), mix(c, d, fracao.x), fracao.y);
}

float campo(vec2 p) {
    float valor = 0.0;
    float amplitude = 0.55;
    for (int i = 0; i < 3; i++) {
        valor += amplitude * ruido(p);
        p = p * 2.05 + vec2(5.2, 1.3);
        amplitude *= 0.5;
    }
    return valor;
}

// Nuvem de pontos neon: interior esparso + contorno denso. A cor vem da música
// e escorre ao longo do corpo; o grave engrossa o contorno e a batida clareia.
vec3 nuvemDePontos(vec2 pixel, float pessoa, float escalaPx, int slot) {
    float grave = musica.x;
    float agudo = musica.z;
    float flash = musica.w;

    vec2 px = 1.0 / resolucao;
    float anel = 0.0;
    float r1 = (11.0 + 7.0 * grave) * escalaPx;
    float r2 = (8.0 + 5.0 * grave) * escalaPx;
    anel += texture(mascara, uv + vec2(px.x * r1, 0.0)).r;
    anel += texture(mascara, uv + vec2(-px.x * r1, 0.0)).r;
    anel += texture(mascara, uv + vec2(0.0, px.y * r1)).r;
    anel += texture(mascara, uv + vec2(0.0, -px.y * r1)).r;
    anel += texture(mascara, uv + vec2(px.x * r2, px.y * r2)).r;
    anel += texture(mascara, uv + vec2(-px.x * r2, px.y * r2)).r;
    anel += texture(mascara, uv + vec2(px.x * r2, -px.y * r2)).r;
    anel += texture(mascara, uv + vec2(-px.x * r2, -px.y * r2)).r;
    float borda = clamp(pessoa * (1.0 - anel * 0.125) * 2.0, 0.0, 1.0);

    vec2 p = pixel / escalaPx;
    float nebulosa = campo(p * 0.012 + vec2(tempo * 0.08, -tempo * 0.044));
    float pessoaMatiz = slot >= 0 ? 0.13 * float(slot) : 0.0;
    float h = matiz + pessoaMatiz + 0.25 * uv.y + 0.35 * nebulosa - 0.04 * tempo;

    // Nuvem de pontos no interior: densidade segue a nebulosa (aglomerados que
    // derivam), o médio adensa, o grave incha os pontos. Sem névoa de fundo.
    float medio = musica.y;
    float escala = 12.0;
    vec2 celula = floor(p / escala);
    vec2 desvio = vec2(hash(celula + 1.7), hash(celula + 8.2)) - 0.5;
    vec2 local = fract(p / escala) - 0.5 - desvio * 0.35;
    float sorte = hash(celula);
    float raio = mix(0.16, 0.30, hash(celula + 3.1)) * (1.0 + 0.3 * grave);
    float disco = 1.0 - smoothstep(raio, raio + 0.12, length(local));
    float densidade = mix(0.16, 0.62, smoothstep(0.32, 0.72, nebulosa)) + 0.2 * medio;
    float interior = disco * step(1.0 - densidade, sorte) * (1.0 - smoothstep(0.15, 0.85, borda));
    interior *= smoothstep(0.35, 0.65, pessoa);
    float claro = step(0.90 - 0.15 * agudo, hash(celula + 19.0));
    float pulso = 0.78 + 0.22 * sin(tempo * 0.6 + sorte * 30.0);
    vec3 corInterior = neon(h + 0.18 * (sorte - 0.5));
    corInterior = mix(corInterior, vec3(1.0), 0.55 * claro + 0.3 * flash);
    corInterior *= interior * mix(0.7, 1.05, nebulosa) * pulso * (0.8 + 0.4 * grave + 0.5 * flash);

    float escalaBorda = 6.0;
    vec2 celulaBorda = floor(p / escalaBorda);
    vec2 localBorda = fract(p / escalaBorda) - 0.5;
    localBorda -= (vec2(hash(celulaBorda + 4.0), hash(celulaBorda + 9.0)) - 0.5) * 0.25;
    float raioBorda = mix(0.22, 0.40, hash(celulaBorda + 2.0));
    float pontoBorda = 1.0 - smoothstep(raioBorda, raioBorda + 0.16, length(localBorda));
    pontoBorda *= step(0.18, hash(celulaBorda)) * smoothstep(0.08, 0.45, borda);
    // Agudo acende alguns pontos quase brancos.
    float destaque = step(0.93 - 0.15 * agudo, hash(celulaBorda + 19.0));
    vec3 cor = neon(h + 0.12 * (hash(celulaBorda + 7.0) - 0.5));
    cor = mix(cor, vec3(1.0), 0.6 * destaque + 0.35 * flash);
    return corInterior + cor * pontoBorda * (0.85 + 0.35 * grave + 0.4 * flash);
}

vec3 maos(vec2 pixel, int i, float alturaPx) {
    vec3 cor = vec3(0.0);
    for (int h = 0; h < 2; h++) {
        vec3 pulso = pontos[i * NUM_PONTOS + (h == 0 ? 9 : 10)];
        if (pulso.z < 0.02) continue;
        // 2.5 alturas de corpo por segundo ~ gesto rápido.
        float energia = clamp((h == 0 ? pessoas[i].y : pessoas[i].z) / 2.5, 0.0, 1.0);
        vec2 dv = pixel - pulso.xy * resolucao;
        float d = length(dv);
        float r = alturaPx * (0.025 + 0.07 * energia) + 2.0;
        float nucleo = exp(-d * d / (r * r));
        float angulo = atan(dv.y, dv.x);
        float raios = pow(ruido(vec2(angulo * 5.0 + float(i * 2 + h) * 13.0, tempo * 1.5 - d / (0.05 * alturaPx + 1.0))), 3.0);
        raios *= exp(-d / (r * 2.5)) * (0.4 + 1.6 * energia);
        vec3 quente = mix(corPessoa(i), vec3(1.0), 0.6);
        cor += (quente * nucleo * 1.4 + corPessoa(i) * raios) * pulso.z;
    }
    return cor;
}

// ---- Bichinho espelho ----------------------------------------------------------
// A criança vira o bicho inteiro: fundo colorido, corpo com pele de desenho
// animado, rosto (olhos, nariz, boca), orelhas/chifres com mola, patas e rabo.
// Tudo é composto por camadas com alpha (o fundo agora é claro).

const vec3 CONTORNO = vec3(0.10, 0.07, 0.20);

float sdTriangulo(vec2 p, vec2 p0, vec2 p1, vec2 p2) {
    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                     vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                     vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

float sdCapsula(vec2 p, vec2 a, vec2 b, float r) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

// Cápsula que afina de ra (em a) para rb (em b).
float sdCapsulaCone(vec2 p, vec2 a, vec2 b, float ra, float rb) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h) - mix(ra, rb, h);
}

float sdCaixa(vec2 p, vec2 meia, float raio) {
    vec2 d = abs(p) - meia + raio;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - raio;
}

float sdElipse(vec2 p, vec2 raios) {
    return (length(p / raios) - 1.0) * min(raios.x, raios.y);
}

vec2 girar(vec2 p, float a) {
    float c = cos(a), s = sin(a);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

// Forma de desenho animado: preenchimento + contorno escuro. sd em pixels.
vec4 forma(float sd, vec3 cor, float contorno) {
    float alpha = 1.0 - smoothstep(-0.8, 0.8, sd);
    float dentro = 1.0 - smoothstep(-contorno - 0.8, -contorno + 0.8, sd);
    return vec4(mix(CONTORNO, cor, dentro), alpha);
}

void sobrepor(inout vec3 cor, vec4 camada) {
    cor = mix(cor, camada.rgb, camada.a);
}

// Paletas por bicho.
vec3 corPele(int a) {
    return a == 0 ? vec3(1.00, 0.63, 0.26) : (a == 1 ? vec3(0.38, 0.86, 0.42) : vec3(0.97, 0.95, 1.00));
}
vec3 corPeleEscura(int a) {
    return a == 0 ? vec3(0.86, 0.40, 0.12) : (a == 1 ? vec3(0.20, 0.62, 0.30) : vec3(0.84, 0.80, 0.95));
}
vec3 corDetalhe(int a) { // parte de dentro da orelha, nariz
    return a == 1 ? vec3(1.00, 0.80, 0.25) : vec3(1.00, 0.55, 0.72);
}

vec3 fundoColorido(int a) {
    vec3 c1, c2, c3;
    if (a == 0) { c1 = vec3(0.55, 0.38, 1.00); c2 = vec3(1.00, 0.48, 0.78); c3 = vec3(0.36, 0.66, 1.00); }
    else if (a == 1) { c1 = vec3(1.00, 0.62, 0.32); c2 = vec3(1.00, 0.86, 0.36); c3 = vec3(0.98, 0.44, 0.52); }
    else { c1 = vec3(0.56, 0.80, 1.00); c2 = vec3(1.00, 0.76, 0.86); c3 = vec3(0.70, 0.98, 0.82); }
    vec2 p = uv * vec2(resolucao.x / resolucao.y, 1.0);
    float n1 = campo(p * 2.2 + vec2(tempo * 0.05, tempo * 0.03));
    float n2 = campo(p * 1.6 - vec2(tempo * 0.04, -tempo * 0.02) + 7.3);
    vec3 cor = mix(c1, c2, smoothstep(0.25, 0.75, n1));
    cor = mix(cor, c3, smoothstep(0.35, 0.80, n2) * 0.7);
    // Bolhas subindo devagar.
    for (int camada = 0; camada < 2; camada++) {
        float escala = camada == 0 ? 7.0 : 4.0;
        vec2 q = p * escala + vec2(0.0, tempo * (camada == 0 ? 0.35 : 0.22));
        vec2 celula = floor(q);
        vec2 local = fract(q) - 0.5;
        float sorte = hash(celula + float(camada) * 17.0);
        if (sorte > 0.55) {
            vec2 centro = (vec2(hash(celula + 3.1), hash(celula + 5.7)) - 0.5) * 0.5;
            float raio = mix(0.10, 0.22, hash(celula + 9.2)) * (1.0 + 0.15 * musica.x);
            float d = length(local - centro);
            float anel = smoothstep(raio, raio - 0.03, d) - smoothstep(raio - 0.04, raio - 0.08, d) * 0.6;
            cor = mix(cor, vec3(1.0), anel * 0.35);
        }
    }
    return cor * (0.92 + 0.10 * musica.x);
}

// Textura da pele presa à pessoa (o padrão anda junto com ela).
vec3 texturaPele(vec2 pixel, int i, int a) {
    float R = max(rig1[i].z * resolucao.y * 0.35, 8.0);
    vec2 P = (pixel - rig1[i].xy * resolucao) / R;
    vec3 base = corPele(a);
    vec3 escura = corPeleEscura(a);
    if (a == 0) {
        // Listras de gato malhado.
        float listra = sin(P.y * 2.2 + sin(P.x * 1.2) * 0.9);
        return mix(base, escura, smoothstep(0.55, 0.75, listra));
    }
    if (a == 1) {
        // Manchas de dinossauro.
        vec2 celula = floor(P * 0.9);
        vec2 local = fract(P * 0.9) - 0.5;
        vec2 centro = (vec2(hash(celula + 1.3), hash(celula + 4.1)) - 0.5) * 0.5;
        float mancha = 1.0 - smoothstep(0.15, 0.19, length(local - centro));
        return mix(base, escura, mancha * step(0.35, hash(celula + 7.7)));
    }
    return mix(base, escura, campo(P * 1.5) * 0.5);
}

// Olho de desenho: branco, pupila que balança e brilho. Pisca fechando em linha.
void olho(inout vec3 cor, vec2 pixel, vec2 centro, float raio, vec2 pupila, float piscar, vec2 d, vec2 cima) {
    float aberto = max(0.08, 1.0 - piscar);
    vec2 v = pixel - centro;
    vec2 q = vec2(dot(v, d), dot(v, cima));
    float sd = sdElipse(q, vec2(raio, raio * 1.15 * aberto));
    sobrepor(cor, forma(sd, vec3(1.0), 2.5));
    if (aberto > 0.3) {
        vec2 c = pupila * raio * 0.38;
        sd = length(q - c) - raio * 0.56 * min(1.0, aberto * 1.3);
        sobrepor(cor, vec4(CONTORNO, 1.0 - smoothstep(-0.8, 0.8, sd)));
        sd = length(q - c - vec2(-0.22, 0.25) * raio) - raio * 0.17;
        sobrepor(cor, vec4(vec3(1.0), 1.0 - smoothstep(-0.8, 0.8, sd)));
    }
}

// Rabo: desenha a corrente de Verlet calculada no C++ (gato e dino); o coelho
// tem um pompom encostado nas costas.
void rabo(inout vec3 cor, vec2 pixel, int i, int a, float U, vec2 dir, vec2 up) {
    vec2 base = raboPontos[i * NUM_ELOS_RABO] * resolucao;
    if (length(pixel - base) > 2.2 * U) return;
    if (a == 2) {
        vec2 c = base + dir * 0.12 * U;
        float ang = atan(pixel.y - c.y, pixel.x - c.x);
        float sd = length(pixel - c) - U * (0.17 + 0.017 * sin(ang * 9.0));
        sobrepor(cor, forma(sd, corPele(a), 3.0));
        return;
    }
    float sdMin = 1e5;
    float listra = 1e5;
    for (int k = 0; k < NUM_ELOS_RABO - 1; k++) {
        float t0 = float(k) / float(NUM_ELOS_RABO - 1);
        float t1 = float(k + 1) / float(NUM_ELOS_RABO - 1);
        vec2 p0 = raboPontos[i * NUM_ELOS_RABO + k] * resolucao;
        vec2 p1 = raboPontos[i * NUM_ELOS_RABO + k + 1] * resolucao;
        float r0 = U * (a == 0 ? mix(0.075, 0.062, t0) : mix(0.24, 0.05, t0));
        float r1 = U * (a == 0 ? mix(0.075, 0.062, t1) : mix(0.24, 0.05, t1));
        float sd = sdCapsulaCone(pixel, p0, p1, r0, r1);
        sdMin = min(sdMin, sd);
        if (k % 2 == 1) listra = min(listra, sd);
    }
    vec3 c = corPele(a);
    if (a == 0) {
        c = mix(c, corPeleEscura(a), step(listra, 0.0)); // anéis escuros no rabo do gato
    }
    sobrepor(cor, forma(sdMin, c, 3.0));
}

vec2 direcaoAngulo(float a) {
    return vec2(cos(a), sin(a));
}

// Personagem chibi: proporções fixas (cabeça do tamanho do corpo, corpo de
// feijãozinho, membros curtos com patas redondas). Só posição, escala e
// ângulos vêm da pessoa. Unidade U = largura dos ombros da pessoa.
void personagem(inout vec3 cor, vec2 pixel, int i, int a) {
    vec4 r1 = rig1[i];
    if (r1.w < 0.05) return;
    float U = r1.z * resolucao.y * corpo[i].z;   // encolhe ao agachar
    vec2 raiz = r1.xy * resolucao;
    if (length(pixel - raiz) > 3.4 * U) return;

    float b = rig2[i].x;
    vec2 dir = vec2(cos(b), sin(b));              // direita do corpo
    vec2 up = vec2(sin(b), -cos(b));              // cima do corpo (y da tela desce)
    // Squash & stretch (princípio clássico de animação): estica ao subir,
    // achata ao descer, com volume constante, e "respira" parado.
    float esticar = rig3[i].x * (1.0 + 0.015 * sin(tempo * 2.4 + float(i)));
    vec2 rel = pixel - raiz;
    pixel = raiz + dir * dot(rel, dir) * esticar + up * dot(rel, up) / esticar;
    vec2 lp = vec2(dot(pixel - raiz, dir), dot(pixel - raiz, up)) / U;
    vec3 detalhe = corDetalhe(a);
    vec3 pele = texturaPele(pixel, i, a);
    vec3 peleClara = mix(corPele(a), vec3(1.0), 0.55);
    float salto = 1.0 + 0.10 * musica.w;

    // 1. Rabo (atrás de tudo)
    rabo(cor, pixel, i, a, U, dir, up);

    // 2. Perninhas curtas com pés
    for (int s = 0; s < 2; s++) {
        float sinal = s == 0 ? -1.0 : 1.0;
        vec2 quadril = raiz + (dir * 0.22 * sinal - up * 0.50) * U;
        vec2 pe = quadril + direcaoAngulo(s == 0 ? rig2[i].z : rig2[i].w) * 0.34 * U;
        float sd = sdCapsula(pixel, quadril, pe, 0.16 * U);
        float sdPe = sdElipse(pixel - pe - vec2(0.05 * sinal, 0.06) * U, vec2(0.22, 0.13) * U);
        sobrepor(cor, forma(min(sd, sdPe), pele, 3.0));
    }

    // 3. Corpo de feijãozinho com barriga clara
    float sdCorpo = sdCapsulaCone(pixel, raiz + up * 0.18 * U, raiz - up * 0.24 * U, 0.40 * U, 0.50 * U);
    sobrepor(cor, forma(sdCorpo, pele, 3.0));
    float sdBarriga = sdElipse(lp - vec2(0.0, -0.12), vec2(0.30, 0.32)) * U;
    sobrepor(cor, vec4(peleClara, 1.0 - smoothstep(-0.8, 0.8, sdBarriga)));

    // 4. Bracinhos com IK de dois ossos: a pata vai aonde a mão da criança
    // aponta; o cotovelo é calculado e sempre dobra para fora (desenho animado).
    for (int s = 0; s < 2; s++) {
        float sinal = s == 0 ? -1.0 : 1.0;
        vec2 ombro = raiz + (dir * 0.34 * sinal + up * 0.24) * U;
        vec2 mao = ombro + (s == 0 ? bracos[i].xy : bracos[i].zw) * U;
        float l1 = 0.34 * U;
        float l2 = 0.32 * U;
        vec2 ombroMao = mao - ombro;
        float dist = clamp(length(ombroMao), 0.05 * U, (l1 + l2) * 0.999);
        vec2 eixo = normalize(ombroMao + vec2(1e-4, 0.0));
        mao = ombro + eixo * dist;
        float avanco = (l1 * l1 - l2 * l2 + dist * dist) / (2.0 * dist);
        float altura = sqrt(max(l1 * l1 - avanco * avanco, 0.0));
        vec2 perp = vec2(-eixo.y, eixo.x);
        vec2 preferido = dir * sinal - up * 0.6;     // para fora e um pouco para baixo
        if (dot(perp, preferido) < 0.0) perp = -perp;
        vec2 cotovelo = ombro + eixo * avanco + perp * altura;
        float sd = min(sdCapsulaCone(pixel, ombro, cotovelo, 0.14 * U, 0.125 * U),
                       sdCapsulaCone(pixel, cotovelo, mao, 0.125 * U, 0.115 * U));
        sobrepor(cor, forma(sd, pele, 3.0));
        float rp = 0.19 * U;
        if (length(pixel - mao) > rp * 2.0) continue;
        sobrepor(cor, forma(length(pixel - mao) - rp, pele, 3.0));
        vec2 frente = normalize(mao - cotovelo + vec2(1e-4, 0.0));
        vec2 lado = vec2(-frente.y, frente.x);
        if (a == 1) {
            // Garrinhas de dino na ponta da pata.
            for (int k = -1; k <= 1; k++) {
                vec2 base = mao + (frente * 0.85 + lado * 0.45 * float(k)) * rp;
                vec2 ponta = base + normalize(frente + lado * 0.3 * float(k)) * rp * 0.45;
                float sdG = sdTriangulo(pixel, base - lado * rp * 0.16, base + lado * rp * 0.16, ponta);
                sobrepor(cor, forma(sdG, vec3(1.0), 1.5));
            }
        } else {
            // Almofadinhas cor-de-rosa.
            sobrepor(cor, forma(sdElipse(pixel - mao + frente * rp * 0.12, vec2(rp * 0.42, rp * 0.34)), detalhe, 1.5));
            for (int k = -1; k <= 1; k++) {
                vec2 dedo = mao + (frente * 0.52 + lado * 0.40 * float(k)) * rp;
                sobrepor(cor, forma(length(pixel - dedo) - rp * 0.16, detalhe, 1.5));
            }
        }
    }

    // 5. Cabeça grande (proporção chibi), apoiada no corpo e inclinando com a cabeça real.
    float hb = rig2[i].y;
    vec2 d = vec2(cos(hb), sin(hb));
    vec2 cima = vec2(d.y, -d.x);
    float H = 0.74 * U;
    vec2 centro = raiz + rig3[i].yz * U;   // cabeça com mola: atrasa e balança
    vec2 v = pixel - centro;
    vec2 q = vec2(dot(v, d), dot(v, cima)) / H;
    float aa = 1.0 / H;
    float orelha = corpo[i].w;
    if (length(q) > 3.0) return;

    for (int s = -1; s <= 1; s += 2) {
        vec2 pe = vec2(q.x * float(s), q.y);
        if (a == 0) {
            vec2 b0 = vec2(0.55, 0.72);
            pe = girar(pe - b0, -orelha * float(s)) / salto + b0;
            float sd = sdTriangulo(pe, vec2(0.12, 0.85), vec2(0.98, 0.40), vec2(0.80, 1.38)) - 0.06;
            sobrepor(cor, forma(sd * H, corPele(a), 3.0));
            sd = sdTriangulo(pe, vec2(0.38, 0.86), vec2(0.84, 0.62), vec2(0.76, 1.18)) - 0.03;
            sobrepor(cor, vec4(detalhe, 1.0 - smoothstep(-aa, aa, sd)));
        } else if (a == 2) {
            vec2 b0 = vec2(0.36, 0.80);
            pe = girar(pe - b0, -orelha * 1.5 * float(s)) / salto + b0;
            float sd = sdCapsulaCone(pe, b0, vec2(0.52, 2.05), 0.22, 0.19);
            sobrepor(cor, forma(sd * H, corPele(a), 3.0));
            sd = sdCapsulaCone(pe, b0 + vec2(0.0, 0.15), vec2(0.50, 1.88), 0.09, 0.08);
            sobrepor(cor, vec4(detalhe, 1.0 - smoothstep(-aa, aa, sd)));
        } else {
            vec2 b0 = vec2(0.45, 0.78);
            pe = girar(pe - b0, -orelha * 0.6 * float(s)) / salto + b0;
            float sd = sdTriangulo(pe, vec2(0.18, 0.88), vec2(0.72, 0.58), vec2(0.62, 1.38)) - 0.05;
            sobrepor(cor, forma(sd * H, detalhe, 3.0));
        }
    }
    if (a == 1) {
        float sd = sdTriangulo(q / salto, vec2(-0.20, 0.92), vec2(0.20, 0.92), vec2(0.0, 1.50)) - 0.05;
        sobrepor(cor, forma(sd * H, detalhe, 3.0));
    }

    // Cabeça lisa (sem listras no rosto, fica mais "fofo") com focinho claro.
    sobrepor(cor, forma(sdElipse(q, vec2(1.06, 0.96)) * H, corPele(a), 3.0));
    if (a == 0) {
        // Três listrinhas finas na testa (gato malhado).
        for (int k = -1; k <= 1; k++) {
            float sd = sdCapsulaCone(q, vec2(0.17 * float(k), 0.86), vec2(0.12 * float(k), 0.58), 0.055, 0.025);
            sobrepor(cor, vec4(corPeleEscura(a), 1.0 - smoothstep(-aa, aa, sd)));
        }
    }
    if (a != 1) {
        float sd = sdElipse(q - vec2(0.0, -0.36), vec2(0.46, 0.32));
        sobrepor(cor, vec4(peleClara, 1.0 - smoothstep(-aa, aa, sd)));
    }

    vec2 pupila = extra[i].xy;
    float piscar = extra[i].z;
    for (int s = -1; s <= 1; s += 2) {
        olho(cor, pixel, centro + (d * 0.38 * float(s) + cima * 0.08) * H, 0.22 * H, pupila, piscar, d, cima);
        float sd = length(q - vec2(0.62 * float(s), -0.30)) - 0.15;
        sobrepor(cor, vec4(vec3(1.0, 0.45, 0.60), 0.55 * (1.0 - smoothstep(-0.06, 0.06, sd))));
    }
    if (a == 1) {
        float sd = sdElipse(q - vec2(0.0, -0.40), vec2(0.48, 0.30));
        sobrepor(cor, forma(sd * H, peleClara, 2.5));
        for (int s = -1; s <= 1; s += 2) {
            sd = length(q - vec2(0.14 * float(s), -0.30)) - 0.05;
            sobrepor(cor, vec4(CONTORNO, 1.0 - smoothstep(-aa, aa, sd)));
        }
        sd = abs(length(q - vec2(0.0, -0.36)) - 0.24) - 0.022;
        sobrepor(cor, vec4(CONTORNO, (1.0 - smoothstep(-aa, aa, sd)) * step(q.y, -0.48)));
        sd = sdTriangulo(q, vec2(0.06, -0.585), vec2(0.15, -0.57), vec2(0.11, -0.68));
        sobrepor(cor, forma(sd * H, vec3(1.0), 1.5));
    } else {
        float sd = sdTriangulo(q, vec2(-0.11, -0.20), vec2(0.11, -0.20), vec2(0.0, -0.32)) - 0.03;
        sobrepor(cor, forma(sd * H, detalhe, 2.0));
        for (int s = -1; s <= 1; s += 2) {
            vec2 c = vec2(0.075 * float(s), -0.38);
            sd = abs(length(q - c) - 0.075) - 0.016;
            sobrepor(cor, vec4(CONTORNO, (1.0 - smoothstep(-aa, aa, sd)) * step(q.y, c.y)));
        }
        if (a == 2) {
            float sdDente = sdCaixa(q - vec2(0.0, -0.53), vec2(0.08, 0.08), 0.02);
            sobrepor(cor, forma(sdDente * H, vec3(1.0), 2.0));
            sdDente = sdCaixa(q - vec2(0.0, -0.53), vec2(0.006, 0.08), 0.0);
            sobrepor(cor, vec4(CONTORNO, 1.0 - smoothstep(-aa, aa, sdDente)));
        }
        for (int s = -1; s <= 1; s += 2) {
            for (int k = -1; k <= 1; k++) {
                sd = sdCapsula(q, vec2(0.28 * float(s), -0.30), vec2(1.10 * float(s), -0.24 + 0.13 * float(k)), 0.012);
                sobrepor(cor, vec4(CONTORNO, 0.85 * (1.0 - smoothstep(-aa, aa, sd))));
            }
        }
    }
}

vec3 cenaBichinho(vec2 pixel) {
    int a = int(animal + 0.5);
    vec3 cor = mix(vec3(0.0), fundoColorido(a), efeitoFundo);
    for (int i = 0; i < MAX_PESSOAS; i++) {
        float presenca = pessoas[i].x;
        if (presenca < 0.05 || rig1[i].w < 0.05) continue;
        vec3 antes = cor;
        personagem(cor, pixel, i, a);
        cor = mix(antes, cor, presenca);
    }
    return cor;
}


// ---- Ecos em quadrinhos ------------------------------------------------------
// A pessoa vira HQ (cor chapada + traço XDoG, vindos do C++) e deixa para trás
// "fantasmas de movimento" em cores pop com retícula Ben-Day, sobre um fundo
// pop-art com raios de explosão.
const vec3 TINTA = vec3(0.06, 0.05, 0.10);

float ecoAmostra(int j, vec2 p) {
    return j < 4 ? texture(ecosA, p)[j] : texture(ecosB, p)[j - 4];
}

// Retícula Ben-Day: grade girada 45°; raio em fração da célula (0..0.7).
float reticula(vec2 pixel, float tamanho, float raio) {
    vec2 r = mat2(0.7071, -0.7071, 0.7071, 0.7071) * pixel / tamanho;
    vec2 local = fract(r) - 0.5;
    return 1.0 - smoothstep(raio - 0.05, raio + 0.05, length(local));
}

vec3 fundoPop(vec2 pixel, float escalaPx) {
    vec2 v = pixel - centroCena * resolucao;
    float angulo = atan(v.y, v.x);
    float distancia = length(v) / resolucao.y;
    // Base creme puxando para a cor da música.
    vec3 base = mix(vec3(1.0, 0.94, 0.62), neon(matiz + 0.5), 0.30);
    float raios = step(0.5, fract(angulo / 6.2831853 * 16.0 + tempo * 0.015));
    vec3 cor = mix(base, base * vec3(0.90, 0.84, 0.80), raios);
    // Retícula que cresce do centro para as bordas, e pulsa com o grave.
    float pontos = reticula(pixel, 16.0 * escalaPx, clamp(distancia * 0.42 + 0.05 * musica.x, 0.04, 0.40));
    return mix(cor, cor * 0.80, pontos);
}

// Contorno de uma silhueta: dentro dela e com algum vizinho fora.
float bordaSilhueta(float a, float vizinhoMinimo) {
    return smoothstep(0.35, 0.65, a) * (1.0 - smoothstep(0.35, 0.65, vizinhoMinimo));
}

vec3 cenaQuadrinhos(vec2 pixel, float pessoa, float escalaPx) {
    vec3 cor = fundoPop(pixel, escalaPx);
    vec2 px = (3.5 * escalaPx) / resolucao;

    // Ecos, do mais antigo para o mais novo.
    for (int j = 7; j >= 0; j--) {
        if (float(j) >= numEcos) continue;
        float a = ecoAmostra(j, uv);
        if (a < 0.02) continue;
        float vizinho = min(min(ecoAmostra(j, uv + vec2(px.x, 0.0)), ecoAmostra(j, uv - vec2(px.x, 0.0))),
                            min(ecoAmostra(j, uv + vec2(0.0, px.y)), ecoAmostra(j, uv - vec2(0.0, px.y))));
        float dentro = smoothstep(0.35, 0.65, a);
        float idade = (float(j) + 1.0) / (numEcos + 1.0);
        vec3 tom = neon(matiz + 0.33 * float(j));
        // A retícula encolhe com a idade: o eco "desbota" como impressão.
        float pontos = reticula(pixel, 9.0 * escalaPx, mix(0.50, 0.14, idade) + 0.05 * musica.x);
        vec3 eco = mix(mix(cor, tom, 0.25 * (1.0 - idade)), tom, pontos);
        eco = mix(eco, TINTA, bordaSilhueta(a, vizinho) * (1.0 - 0.6 * idade));
        cor = mix(cor, eco, dentro);
    }

    // Carimbo da batida: quadro "congelado" branco com contorno grosso.
    if (carimboForca > 0.01) {
        float a = texture(carimbo, uv).r;
        if (a > 0.02) {
            vec2 px2 = px * 1.6;
            float vizinho = min(min(texture(carimbo, uv + vec2(px2.x, 0.0)).r, texture(carimbo, uv - vec2(px2.x, 0.0)).r),
                                min(texture(carimbo, uv + vec2(0.0, px2.y)).r, texture(carimbo, uv - vec2(0.0, px2.y)).r));
            vec3 branco = mix(vec3(1.0), TINTA, bordaSilhueta(a, vizinho));
            cor = mix(cor, branco, smoothstep(0.35, 0.65, a) * carimboForca);
        }
    }

    // Pessoa em HQ: cor chapada, retícula nas sombras, traço e contorno grosso.
    float corpo = smoothstep(0.35, 0.65, pessoa);
    if (corpo > 0.01) {
        vec4 q = texture(quadrinho, uv);
        vec3 c = q.rgb;
        float luminancia = dot(c, vec3(0.299, 0.587, 0.114));
        float sombra = reticula(pixel, 6.0 * escalaPx, clamp((0.62 - luminancia) * 0.85, 0.0, 0.45));
        c = mix(c, c * 0.30, sombra);
        c = mix(c, TINTA, smoothstep(0.30, 0.70, q.a));
        float vizinho = min(min(texture(mascara, uv + vec2(px.x, 0.0)).r, texture(mascara, uv - vec2(px.x, 0.0)).r),
                            min(texture(mascara, uv + vec2(0.0, px.y)).r, texture(mascara, uv - vec2(0.0, px.y)).r));
        c = mix(c, TINTA, bordaSilhueta(pessoa, vizinho));
        cor = mix(cor, c, corpo);
    }
    return cor;
}

// ---- LEGO --------------------------------------------------------------------
// Mosaico de peças 1x2 vistas de cima (pinos em relevo, bisel e sombra) sobre
// uma placa-base. A cor de cada peça vem da câmera, ajustada à paleta LEGO.
// Peças de onde a pessoa saiu "se soltam" e caem (histórico de silhuetas).
const vec3 PALETA_LEGO[18] = vec3[18](
    vec3(0.96, 0.96, 0.96), vec3(0.63, 0.65, 0.64), vec3(0.39, 0.39, 0.40), vec3(0.11, 0.11, 0.12),
    vec3(0.79, 0.10, 0.07), vec3(0.45, 0.07, 0.08), vec3(1.00, 0.53, 0.15), vec3(0.98, 0.80, 0.18),
    vec3(0.89, 0.80, 0.62), vec3(0.35, 0.23, 0.15), vec3(0.14, 0.48, 0.25), vec3(0.65, 0.79, 0.20),
    vec3(0.00, 0.33, 0.75), vec3(0.10, 0.20, 0.40), vec3(0.36, 0.62, 0.85), vec3(0.99, 0.59, 0.75),
    vec3(0.98, 0.80, 0.66), vec3(0.80, 0.55, 0.38)  // nougat claro e médio (tons de pele)
);

vec3 corLego(vec3 c) {
    vec3 melhor = PALETA_LEGO[0];
    float menor = 1e9;
    for (int k = 0; k < 18; k++) {
        vec3 d = (c - PALETA_LEGO[k]) * vec3(0.30, 0.59, 0.11) * 3.0 + (c - PALETA_LEGO[k]) * 0.6;
        float distancia = dot(d, d);
        if (distancia < menor) {
            menor = distancia;
            melhor = PALETA_LEGO[k];
        }
    }
    return melhor;
}

// Desenha uma peça 1x2 (local em [0, 2S) x [0, S)) com bisel e dois pinos.
vec3 pecaLego(vec2 local, float S, vec3 base) {
    // Fresta escura entre peças.
    float margem = min(min(local.x, 2.0 * S - local.x), min(local.y, S - local.y));
    if (margem < 0.05 * S) return base * 0.35;
    // Bisel: luz em cima/esquerda, sombra embaixo/direita.
    vec3 cor = base;
    float borda = 0.11 * S;
    if (local.y < borda || local.x < borda) cor *= 1.15;
    if (S - local.y < borda || 2.0 * S - local.x < borda) cor *= 0.78;
    for (int k = 0; k < 2; k++) {
        vec2 centro = vec2(S * (float(k) + 0.5), S * 0.5);
        vec2 v = local - centro;
        float r = 0.31 * S;
        // Sombra do pino (deslocada para baixo e à direita).
        float sombra = 1.0 - smoothstep(r - 0.03 * S, r + 0.03 * S, length(v - vec2(0.05, 0.07) * S));
        cor = mix(cor, base * 0.62, sombra * 0.8);
        float pino = 1.0 - smoothstep(r - 0.03 * S, r + 0.03 * S, length(v));
        // Topo do pino com um brilho no canto de cima.
        float brilho = 1.0 - smoothstep(0.0, r * 0.9, length(v - vec2(-0.12, -0.12) * S));
        vec3 topo = base * 1.08 + vec3(0.25) * brilho * brilho;
        cor = mix(cor, topo, pino);
    }
    return cor;
}

vec3 cenaLego(vec2 pixel, float escalaPx) {
    float S = tamanhoPeca * escalaPx;
    // Placa-base: um pino em cada célula, bem escura.
    vec2 celula = fract(pixel / S) * S;
    vec3 placa = vec3(0.13, 0.15, 0.20) * (0.95 + 0.10 * musica.x);
    float pinoPlaca = 1.0 - smoothstep(0.28 * S, 0.32 * S, length(celula - 0.5 * S));
    vec3 cor = mix(placa, placa * 1.35, pinoPlaca);

    // Peça 1x2 desta posição (fileiras alternadas, como parede de tijolos).
    float fileira = floor(pixel.y / S);
    float deslocamento = mod(fileira, 2.0) * S;
    float coluna = floor((pixel.x + deslocamento) / (2.0 * S));
    vec2 origem = vec2(coluna * 2.0 * S - deslocamento, fileira * S);
    vec2 centroUv = (origem + vec2(S, 0.5 * S)) / resolucao;
    vec2 local = pixel - origem;

    float presente = texture(mascara, centroUv).r;
    if (presente > 0.5) {
        vec2 pino1 = (origem + vec2(0.5 * S, 0.5 * S)) / resolucao;
        vec2 pino2 = (origem + vec2(1.5 * S, 0.5 * S)) / resolucao;
        vec3 c = 0.5 * (texture(camera, pino1).rgb + texture(camera, pino2).rgb);
        // Ganho automático (sala escura) + leve gama e contraste para as cores
        // caírem em peças coloridas e não em preto/cinza.
        c = pow(clamp(c * ganhoCamera, 0.0, 1.0), vec3(0.85));
        c = clamp((c - 0.5) * 1.15 + 0.5, 0.0, 1.0);
        return pecaLego(local, S, corLego(c) * (0.95 + 0.12 * musica.w));
    }

    // Peças que se soltaram: estavam aqui há pouco e a pessoa saiu. Caem com
    // gravidade a partir de onde estavam (a peça inteira cai junta).
    if (legoCair > 0.5) {
        for (int j = 0; j < 8; j++) {
            float idade = (float(j) + 1.0) * 2.0 / 30.0;           // s (2 quadros por eco)
            float queda = 0.5 * 2600.0 * escalaPx * idade * idade;  // px
            vec2 p0 = pixel - vec2(0.0, queda);
            float f0 = floor(p0.y / S);
            float d0 = mod(f0, 2.0) * S;
            float c0 = floor((p0.x + d0) / (2.0 * S));
            vec2 o0 = vec2(c0 * 2.0 * S - d0, f0 * S);
            vec2 centro0 = (o0 + vec2(S, 0.5 * S)) / resolucao;
            if (ecoAmostra(j, centro0) > 0.5 && texture(mascara, centro0).r < 0.5) {
                float id = hash(vec2(c0, f0));
                vec3 base = PALETA_LEGO[int(id * 15.99)];
                vec3 peca = pecaLego(p0 - o0, S, base);
                return mix(cor, peca, 1.0 - smoothstep(0.35, 0.55, idade));
            }
        }
    }
    return cor;
}

void main() {
    vec2 pixel = uv * resolucao;
    float escalaPx = resolucao.y / 720.0;
    vec4 amostra = texture(mascara, uv);
    float pessoa = amostra.r;
    int slot = int(amostra.g * 255.0 + 0.5) - 1;

    // Efeito 1: luz reativa (aditiva sobre preto).
    vec3 luz = vec3(0.0);
    if (efeitoNuvem > 0.01 || efeitoMaos > 0.01) {
        if (pessoa > 0.02 && efeitoNuvem > 0.01) {
            luz += nuvemDePontos(pixel, pessoa, escalaPx, slot) * efeitoNuvem;
        }
        for (int i = 0; i < MAX_PESSOAS && efeitoMaos > 0.01; i++) {
            float presenca = pessoas[i].x;
            if (presenca < 0.01) continue;
            luz += maos(pixel, i, pessoas[i].w * resolucao.y) * efeitoMaos * presenca;
        }
        luz = 1.0 - exp(-luz * 1.3 * brilhoGeral);
    }

    // Efeito 2: bichinho espelho (cena com fundo e camadas).
    vec3 cor = luz;
    if (efeitoBichinho > 0.01) {
        vec3 bicho = clamp(cenaBichinho(pixel) * brilhoGeral, 0.0, 1.0);
        cor = mix(luz, bicho, efeitoBichinho);
    }
    // Efeito 5: LEGO (cena opaca).
    if (efeitoLego > 0.01) {
        vec3 lego = clamp(cenaLego(pixel, escalaPx) * brilhoGeral, 0.0, 1.0);
        cor = mix(cor, lego, efeitoLego);
    }
    // Efeito 3: ecos em quadrinhos (cena opaca).
    if (efeitoEcos > 0.01) {
        vec3 hq = clamp(cenaQuadrinhos(pixel, pessoa, escalaPx) * brilhoGeral, 0.0, 1.0);
        cor = mix(cor, hq, efeitoEcos);
    }
    corFinal = vec4(cor, 1.0);
}
)";

// Pares de keypoints desenhados no modo debug.
const std::array<std::pair<int, int>, 16> OSSOS_DEBUG{{
    {5, 6}, {5, 7}, {7, 9}, {6, 8}, {8, 10}, {5, 11}, {6, 12}, {11, 12},
    {11, 13}, {13, 15}, {12, 14}, {14, 16}, {0, 1}, {0, 2}, {1, 3}, {2, 4},
}};

float aproximar(float atual, float alvo, float dt, float tau) {
    return atual + (alvo - atual) * (1.0f - std::exp(-dt / tau));
}

} // namespace

void ofApp::setup() {
    ofDisableArbTex();
    ofSetVerticalSync(true);
    ofSetFrameRate(60);
    ofBackground(0);
    atualizarCursor();

    mascaraTexture.allocate(config::MASCARA_LARGURA, config::MASCARA_ALTURA, GL_RG8);
    ecosATexture.allocate(config::MASCARA_LARGURA, config::MASCARA_ALTURA, GL_RGBA8);
    ecosBTexture.allocate(config::MASCARA_LARGURA, config::MASCARA_ALTURA, GL_RGBA8);
    carimboTexture.allocate(config::MASCARA_LARGURA, config::MASCARA_ALTURA, GL_R8);
    quadrinhoTexture.allocate(config::MASCARA_LARGURA, config::MASCARA_ALTURA, GL_RGBA8);
    for (ofTexture* textura : {&ecosATexture, &ecosBTexture, &carimboTexture}) {
        textura->setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    }
    cameraTexture.allocate(config::CAMERA_LARGURA, config::CAMERA_ALTURA, GL_RGB8);
    atualizarQuad(ofGetWidth(), ofGetHeight());
    criarShader();

    const std::string pastaExecutavel = ofFilePath::getCurrentExeDir();
    processador = std::make_unique<ProcessadorVisao>(
        pastaExecutavel + config::MODELO_SEGMENTACAO, pastaExecutavel + config::MODELO_POSE
    );
    debug = std::getenv("VISUAL_FRAMES_DEBUG") != nullptr;
    processador->definirDebug(debug);
    processador->iniciar();

    audio = std::make_unique<AnalisadorAudio>(config::AUDIO_DISPOSITIVO);
    audio->iniciar();
}

void ofApp::update() {
    const float dt = static_cast<float>(ofGetLastFrameTime());
    if (processador->obterSeNovo(resultado.sequencia, resultado)) {
        mascaraTexture.loadData(resultado.mascara.data, config::MASCARA_LARGURA, config::MASCARA_ALTURA, GL_RG);
        atualizarEcos();
        if (!resultado.quadrinho.empty()) {
            quadrinhoTexture.loadData(resultado.quadrinho.data, config::MASCARA_LARGURA, config::MASCARA_ALTURA, GL_RGBA);
        }
        const bool legoVisivel = efeitoLego > 0.01f || estado->efeito == Efeito::Lego;
        if ((debug || legoVisivel) && !resultado.frameDebug.empty()) {
            cameraTexture.loadData(
                resultado.frameDebug.data, config::CAMERA_LARGURA, config::CAMERA_ALTURA, GL_BGR
            );
        }
        if (legoVisivel && !resultado.frameDebug.empty()) {
            // Brilho médio só dentro da silhueta: em sala escura a pessoa sai
            // quase preta e as peças virariam "sombra".
            cv::Mat pequeno;
            cv::resize(resultado.frameDebug, pequeno, cv::Size(config::MASCARA_LARGURA, config::MASCARA_ALTURA), 0, 0, cv::INTER_AREA);
            cv::Mat alpha;
            cv::extractChannel(resultado.mascara, alpha, 0);
            const cv::Scalar media = cv::mean(pequeno, alpha > 128);
            const float luminancia = (0.114f * media[0] + 0.587f * media[1] + 0.299f * media[2]) / 255.0f;
            const float alvo = luminancia > 0.01f ? std::clamp(0.5f / luminancia, 1.0f, 4.0f) : 1.0f;
            ganhoCamera += (alvo - ganhoCamera) * 0.2f;
        }
        quadroNovo = true;
    }
    atualizarSlots(dt);
    atualizarMusica(dt);
    // Os efeitos são independentes: só o escolhido no painel fica visível, e a
    // troca é um fade (o efeito antigo some enquanto o novo entra).
    const bool luz = estado->efeito == Efeito::Luz;
    const bool bichinho = estado->efeito == Efeito::Bichinho;
    const bool ecos = estado->efeito == Efeito::Ecos;
    efeitoNuvem = aproximar(efeitoNuvem, luz ? 1.0f : 0.0f, dt, 0.25f);
    efeitoEcos = aproximar(efeitoEcos, ecos ? 1.0f : 0.0f, dt, 0.25f);
    const bool paraquedas = estado->efeito == Efeito::Paraquedas;
    efeitoParaquedas = aproximar(efeitoParaquedas, paraquedas ? 1.0f : 0.0f, dt, 0.25f);
    const bool lego = estado->efeito == Efeito::Lego;
    efeitoLego = aproximar(efeitoLego, lego ? 1.0f : 0.0f, dt, 0.25f);
    // O traço XDoG é calculado na thread de visão só quando o efeito usa.
    processador->definirQuadrinho(ecos || efeitoEcos > 0.01f);
    // Quadro da câmera: rostos (paraquedas, cabeçudos) e cores das peças (LEGO).
    const bool precisaRostos = paraquedas || efeitoParaquedas > 0.01f;
    processador->definirEnviarQuadro(precisaRostos || lego || efeitoLego > 0.01f);
    if (quadroNovo && precisaRostos && !resultado.frameDebug.empty()) {
        recortarRostos(resultado.frameDebug);
    }
    quadroNovo = false;
    atualizarParaquedistas(dt);
    atualizarCentroCena(dt);
    efeitoMaos = aproximar(efeitoMaos, luz && estado->orbesMaos ? 1.0f : 0.0f, dt, 0.25f);
    efeitoBichinho = aproximar(efeitoBichinho, bichinho ? 1.0f : 0.0f, dt, 0.25f);
    efeitoFundo = aproximar(efeitoFundo, estado->fundoColorido ? 1.0f : 0.0f, dt, 0.25f);
    brilho = estado->brilho;
    atualizarEstadoPainel();
}

void ofApp::atualizarEstadoPainel() {
    estado->visaoMs = resultado.sequencia > 0 ? static_cast<float>(resultado.quadroMs) : 0.0f;
    estado->usandoCuda = resultado.usandoCuda;
    estado->pessoas = static_cast<int>(resultado.pessoas.size());
    estado->audioAtivo = niveis.ativo;
    estado->grave = niveis.grave;
    estado->medio = niveis.medio;
    estado->agudo = niveis.agudo;
}

// Histórico de silhuetas (canal alpha da máscara, um por quadro da câmera) e
// montagem dos ecos: o eco j tem atraso (j + 1) × passo quadros. Os 8 ecos vão
// para o shader em duas texturas RGBA, um por canal.
void ofApp::atualizarEcos() {
    cv::Mat alpha;
    cv::extractChannel(resultado.mascara, alpha, 0);
    historicoMascaras.push_front(alpha);
    constexpr std::size_t MAXIMO = 8 * 8 + 1; // 8 ecos × passo máximo 8
    while (historicoMascaras.size() > MAXIMO) {
        historicoMascaras.pop_back();
    }
    const bool lego = estado->efeito == Efeito::Lego || efeitoLego > 0.01f;
    if (estado->efeito != Efeito::Ecos && efeitoEcos < 0.01f && !lego) {
        return; // só monta as texturas quando o efeito está visível
    }
    // No LEGO os ecos são as peças que caem: 2 quadros entre um e outro.
    const int passo = lego ? 2 : std::max(1, static_cast<int>(std::round(estado->distanciaEcos)));
    const cv::Mat vazio = cv::Mat::zeros(alpha.size(), CV_8U);
    auto eco = [&](int j) -> const cv::Mat& {
        const std::size_t indice = static_cast<std::size_t>((j + 1) * passo);
        return indice < historicoMascaras.size() ? historicoMascaras[indice] : vazio;
    };
    cv::Mat a;
    cv::Mat b;
    cv::merge(std::vector<cv::Mat>{eco(0), eco(1), eco(2), eco(3)}, a);
    cv::merge(std::vector<cv::Mat>{eco(4), eco(5), eco(6), eco(7)}, b);
    ecosATexture.loadData(a.data, a.cols, a.rows, GL_RGBA);
    ecosBTexture.loadData(b.data, b.cols, b.rows, GL_RGBA);
}

void ofApp::atualizarSlots(float dt) {
    for (int s = 0; s < config::MAX_PESSOAS; ++s) {
        Slot& slot = slots[s];
        const PessoaVisivel* pessoa = nullptr;
        for (const auto& candidata : resultado.pessoas) {
            if (candidata.slot == s) {
                pessoa = &candidata;
                break;
            }
        }
        float alvo = 0;
        float alvoEnergiaE = 0;
        float alvoEnergiaD = 0;
        if (pessoa != nullptr) {
            if (slot.id != pessoa->id) {
                slot = Slot{};
                slot.id = pessoa->id;
            }
            alvo = pessoa->ativa ? 1.0f : 0.0f;
            alvoEnergiaE = pessoa->velocidadePulsoE;
            alvoEnergiaD = pessoa->velocidadePulsoD;
            slot.alturaCorpo = pessoa->caixa.height / config::CAMERA_ALTURA;
            for (int k = 0; k < NUM_KEYPOINTS; ++k) {
                const PontoChave novo{
                    pessoa->pontos[k].x / config::CAMERA_LARGURA,
                    pessoa->pontos[k].y / config::CAMERA_ALTURA,
                    pessoa->pontos[k].confianca,
                };
                slot.pontos[k] = novo;
            }
        }
        // Entra rápido, sai devagar; mesma assimetria para a energia das mãos.
        slot.presenca = aproximar(slot.presenca, alvo, dt, alvo > slot.presenca ? 0.12f : 0.45f);
        slot.energiaE = aproximar(slot.energiaE, alvoEnergiaE, dt, alvoEnergiaE > slot.energiaE ? 0.06f : 0.5f);
        slot.energiaD = aproximar(slot.energiaD, alvoEnergiaD, dt, alvoEnergiaD > slot.energiaD ? 0.06f : 0.5f);
        atualizarBichinho(slot, dt);
    }
}

void ofApp::atualizarBichinho(Slot& slot, float dt) {
    constexpr float L = config::CAMERA_LARGURA;
    constexpr float A = config::CAMERA_ALTURA;
    auto ponto = [&](int k) { return glm::vec2(slot.pontos[k].x * L, slot.pontos[k].y * A); };
    auto confianca = [&](int k) { return slot.pontos[k].confianca; };

    // Cabeça: média ponderada de nariz, olhos e orelhas.
    glm::vec2 soma{0, 0};
    float peso = 0;
    for (int k = NARIZ; k <= ORELHA_D; ++k) {
        soma += ponto(k) * confianca(k);
        peso += confianca(k);
    }
    const bool temCabeca = peso > 0.8f;

    // Eixo dos olhos (ou das orelhas) dá a inclinação; vira sempre para a direita da tela.
    glm::vec2 direcao = slot.cabecaDir;
    float raioPx = slot.cabecaRaio * A;
    if (confianca(OLHO_E) > 0.3f && confianca(OLHO_D) > 0.3f) {
        const glm::vec2 eixo = ponto(OLHO_D) - ponto(OLHO_E);
        if (glm::length(eixo) > 2.0f) {
            direcao = glm::normalize(eixo);
        }
        raioPx = 1.3f * glm::length(eixo);
    }
    if (confianca(ORELHA_E) > 0.3f && confianca(ORELHA_D) > 0.3f) {
        raioPx = 0.55f * glm::length(ponto(ORELHA_D) - ponto(ORELHA_E));
    }
    if (direcao.x < 0) {
        direcao = -direcao;
    }
    raioPx = std::max(raioPx, 0.04f * slot.alturaCorpo * A);

    if (temCabeca) {
        glm::vec2 centro = soma / peso;
        // A média fica abaixo do centro do crânio (o nariz puxa para baixo).
        centro -= glm::vec2(direcao.y, -direcao.x) * raioPx * 0.10f;
        const glm::vec2 centroUv(centro.x / L, centro.y / A);
        if (!slot.cabecaIniciada) {
            slot.cabecaIniciada = true;
            slot.cabecaCentro = centroUv;
            slot.cabecaRaio = raioPx / A;
            slot.cabecaDir = direcao;
        } else {
            slot.cabecaCentro += (centroUv - slot.cabecaCentro) * (1.0f - std::exp(-dt / 0.04f));
            slot.cabecaRaio = aproximar(slot.cabecaRaio, raioPx / A, dt, 0.15f);
            const glm::vec2 suave = slot.cabecaDir + (direcao - slot.cabecaDir) * (1.0f - std::exp(-dt / 0.08f));
            slot.cabecaDir = glm::length(suave) > 1e-3f ? glm::normalize(suave) : direcao;
        }
    }
    slot.cabecaValida = aproximar(slot.cabecaValida, temCabeca ? slot.presenca : 0.0f, dt, 0.15f);

    // Agachar: a altura da caixa cai em relação à maior vista (a pessoa em pé).
    slot.alturaRef = std::max(slot.alturaCorpo, slot.alturaRef * std::exp(-dt / 30.0f));
    const float razao = slot.alturaRef > 0 ? slot.alturaCorpo / slot.alturaRef : 1.0f;
    const float alvoEscala = 0.25f + 0.75f * glm::smoothstep(0.55f, 0.90f, razao);
    slot.escala = aproximar(slot.escala, alvoEscala, dt, 0.15f);

    // Velocidade da cabeça (px da câmera/s) move as molas e as pupilas.
    const glm::vec2 centroPx(slot.cabecaCentro.x * L, slot.cabecaCentro.y * A);
    glm::vec2 velocidade{0, 0};
    if (slot.temCentroAnterior && dt > 0) {
        velocidade = (centroPx - slot.centroAnterior) / dt;
    }
    slot.centroAnterior = centroPx;
    slot.temCentroAnterior = temCabeca;

    // Orelhas: mola amortecida (oscila um pouco e volta), empurrada pelo
    // movimento lateral da cabeça e por um "pulo" em cada batida da música.
    auto mola = [dt](float& angulo, float& vel, float alvo, float rigidez, float amortecimento) {
        const float passo = std::min(dt, 1.0f / 30.0f);
        vel += (rigidez * (alvo - angulo) - amortecimento * vel) * passo;
        angulo += vel * passo;
        angulo = std::clamp(angulo, -0.9f, 0.9f);
    };
    const float alvoOrelha = std::clamp(-velocidade.x * 0.0012f, -0.5f, 0.5f);
    if (flash > 0.95f) {
        slot.orelhaVelocidade += 2.5f;
    }
    mola(slot.orelhaAngulo, slot.orelhaVelocidade, alvoOrelha, 70.0f, 6.0f);

    // Pupilas "googly": ficam para trás do movimento e voltam devagar.
    glm::vec2 alvoPupila = -velocidade * 0.0015f;
    if (glm::length(alvoPupila) > 0.6f) {
        alvoPupila = glm::normalize(alvoPupila) * 0.6f;
    }
    slot.pupila += (alvoPupila - slot.pupila) * (1.0f - std::exp(-dt / 0.10f));

    // Piscar a cada 2,5–5,5 s, durando 0,18 s.
    slot.relogioPiscar += dt;
    if (slot.proximoPiscar <= 0) {
        slot.proximoPiscar = ofRandom(2.5f, 5.5f);
    }
    constexpr float DURACAO_PISCAR = 0.18f;
    if (slot.relogioPiscar > slot.proximoPiscar) {
        const float t = slot.relogioPiscar - slot.proximoPiscar;
        slot.piscar = t < DURACAO_PISCAR ? std::sin(PI * t / DURACAO_PISCAR) : 0.0f;
        if (t >= DURACAO_PISCAR) {
            slot.relogioPiscar = 0;
            slot.proximoPiscar = ofRandom(2.5f, 5.5f);
        }
    }

    // Rabo: balança sozinho e chicoteia com o movimento do quadril.
    slot.relogioRabo += dt;
    const float alvoRabo = std::clamp(-velocidade.x * 0.0015f, -0.6f, 0.6f)
        + 0.25f * std::sin(slot.relogioRabo * 2.2f + slot.id);
    mola(slot.raboAngulo, slot.raboVelocidade, alvoRabo, 25.0f, 4.0f);

    atualizarRig(slot, dt);
}

// Rig chibi: proporções fixas de desenho (cabeça do tamanho do corpo, membros
// curtos). Da pessoa só vêm posição, escala e direções dos membros
// (retargeting, como no Animated Drawings da Meta), nunca os comprimentos.
//
// Movimento: cada parte segue o seu alvo com dinâmica de segunda ordem
// (t3ssel8r) — tem massa, passa um pouquinho do ponto e se acomoda — em vez de
// uma média exponencial. A cabeça é mais "mole" que o corpo (atrasa e balança),
// o corpo inclina para o lado em que anda e o rabo é uma corrente de Verlet
// (como os spring bones do VRM).
void ofApp::atualizarRig(Slot& slot, float dt) {
    constexpr float L = config::CAMERA_LARGURA;
    constexpr float A = config::CAMERA_ALTURA;
    dt = std::min(dt, 1.0f / 20.0f); // quadro travado não pode explodir as molas
    auto ponto = [&](int k) { return glm::vec2(slot.pontos[k].x * L, slot.pontos[k].y * A); };
    auto confianca = [&](int k) { return slot.pontos[k].confianca; };
    auto visivel = [&](int k) { return confianca(k) > 0.45f; };

    const bool temOmbros = visivel(OMBRO_E) && visivel(OMBRO_D);
    const bool temQuadril = visivel(QUADRIL_E) && visivel(QUADRIL_D);

    // ---- Alvos de posição e escala (só dos ombros; sem ombros, segura) ----
    if (temOmbros) {
        const float largura = glm::length(ponto(OMBRO_E) - ponto(OMBRO_D));
        // O boneco inteiro tem ~2,6 U de altura; limita para caber na tela.
        slot.alvoU = std::min(largura * 1.05f, 0.30f * A) / A;
        glm::vec2 raizPx = 0.5f * (ponto(OMBRO_E) + ponto(OMBRO_D)) + glm::vec2(0, largura * 0.35f);
        // Mantém o boneco inteiro na tela, mesmo com a criança na borda ou sentada.
        const float uPx = slot.alvoU * A;
        raizPx.x = std::clamp(raizPx.x, 0.9f * uPx, L - 0.9f * uPx);
        raizPx.y = std::clamp(raizPx.y, 1.75f * uPx, A - 0.95f * uPx);
        slot.alvoRaiz = {raizPx.x / L, raizPx.y / A};
        if (!slot.rigIniciado) {
            slot.rigIniciado = true;
            slot.dinU.reiniciar(slot.alvoU);
            slot.dinRaiz.reiniciar(slot.alvoRaiz);
            slot.dinCabeca.reiniciar(slot.alvoRaiz - glm::vec2(0, 0.98f * slot.alvoU));
            slot.raboIniciado = false;
        }
    }
    if (!slot.rigIniciado) {
        slot.rigValido = aproximar(slot.rigValido, 0.0f, dt, 0.2f);
        return;
    }
    const bool valido = temOmbros || slot.cabecaValida > 0.3f;
    slot.rigValido = aproximar(slot.rigValido, valido ? slot.presenca : 0.0f, dt, valido ? 0.15f : 0.4f);

    // Escala: lenta e sem balanço. Raiz: rápida, quase sem passar do ponto.
    slot.rigU = slot.dinU.atualizar(dt, slot.alvoU);
    slot.rigRaiz = slot.dinRaiz.atualizar(dt, slot.alvoRaiz);
    const float uPx = std::max(slot.rigU * A, 1.0f);
    const glm::vec2 velocidadeRaiz = slot.dinRaiz.velocidadeAtual() * glm::vec2(L, A) / uPx; // U/s

    // Squash & stretch pela velocidade vertical (y da tela desce).
    const float alvoEsticar = std::clamp(1.0f - velocidadeRaiz.y * 0.06f, 0.86f, 1.16f);
    slot.rigEsticar = slot.dinEsticar.atualizar(dt, alvoEsticar);

    // Corpo: inclinação real (quadril -> ombros) + inclina para o lado em que anda.
    float alvoCorpo = 0;
    if (temOmbros && temQuadril) {
        const glm::vec2 v = 0.5f * (ponto(OMBRO_E) + ponto(OMBRO_D)) - 0.5f * (ponto(QUADRIL_E) + ponto(QUADRIL_D));
        alvoCorpo = std::clamp(std::atan2(v.x, -v.y), -0.3f, 0.3f);
    }
    alvoCorpo += std::clamp(velocidadeRaiz.x * 0.07f, -0.22f, 0.22f);
    slot.rigCorpoAngulo = slot.dinCorpo.atualizar(dt, alvoCorpo);
    const float b = slot.rigCorpoAngulo;
    const glm::vec2 direita(std::cos(b), std::sin(b));
    const glm::vec2 cima(std::sin(b), -std::cos(b));

    // Cabeça: segue o ponto acima do corpo com uma mola mais mole, então
    // atrasa nos movimentos e balança um pouco antes de parar.
    const glm::vec2 raizPx = slot.rigRaiz * glm::vec2(L, A);
    const glm::vec2 alvoCabeca = (raizPx + cima * 0.98f * uPx) / glm::vec2(L, A);
    const glm::vec2 cabecaPx = slot.dinCabeca.atualizar(dt, alvoCabeca) * glm::vec2(L, A);
    glm::vec2 deslocamento = (cabecaPx - raizPx) / uPx;
    const glm::vec2 repouso = cima * 0.98f;
    if (glm::length(deslocamento - repouso) > 0.22f) {
        deslocamento = repouso + glm::normalize(deslocamento - repouso) * 0.22f; // não descola do pescoço
    }
    slot.cabecaDeslocamento = deslocamento;
    const float alvoInclinacao = std::clamp(std::atan2(slot.cabecaDir.y, slot.cabecaDir.x), -0.5f, 0.5f);
    slot.cabecaInclinacao = slot.dinInclinacao.atualizar(dt, alvoInclinacao);

    // ---- Braços e pernas ----
    // Qual ombro está à esquerda da TELA, decidido pela geometria com histerese.
    // (Não dá para supor pelo espelhamento: o modelo vê a imagem já espelhada e
    // rotula esquerda/direita pela aparência, o que inverte os lados.)
    if (temOmbros) {
        const float alvo = ponto(OMBRO_E).x < ponto(OMBRO_D).x ? 1.0f : 0.0f;
        slot.ombroEaEsquerda = aproximar(slot.ombroEaEsquerda, alvo, dt, 0.3f);
    }
    const bool eNaEsquerda = slot.ombroEaEsquerda > 0.5f;
    for (int lado = 0; lado < 2; ++lado) {
        const bool usaE = (lado == 0) == eNaEsquerda;
        const int ombro = usaE ? OMBRO_E : OMBRO_D;
        const int cotovelo = usaE ? COTOVELO_E : COTOVELO_D;
        const int pulso = usaE ? PULSO_E : PULSO_D;
        const float sinal = lado == 0 ? -1.0f : 1.0f;

        // Pata: direção ombro->pulso da pessoa × quanto o braço real está
        // esticado. O cotovelo sai da IK no shader. Sem dados, volta ao descanso.
        constexpr float ALCANCE = 0.66f; // braço do boneco esticado, em U
        glm::vec2 alvoMao = glm::normalize(glm::vec2(0.35f * sinal, 1.0f)) * 0.52f;
        if (visivel(ombro) && !visivel(pulso) && visivel(cotovelo)) {
            // Mão fora do quadro (braço aberto perto da câmera): segue o cotovelo.
            const glm::vec2 ombroCotovelo = ponto(cotovelo) - ponto(ombro);
            if (glm::length(ombroCotovelo) > 1.0f) {
                alvoMao = glm::normalize(ombroCotovelo) * ALCANCE * 0.95f;
            }
        } else if (visivel(ombro) && visivel(pulso)) {
            const glm::vec2 ombroPulso = ponto(pulso) - ponto(ombro);
            const float distancia = glm::length(ombroPulso);
            float comprimentoBraco = temOmbros ? 1.6f * glm::length(ponto(OMBRO_E) - ponto(OMBRO_D)) : distancia;
            if (visivel(cotovelo)) {
                comprimentoBraco = glm::length(ponto(cotovelo) - ponto(ombro)) + glm::length(ponto(pulso) - ponto(cotovelo));
            }
            const float esticado = std::clamp(distancia / std::max(comprimentoBraco, 1.0f), 0.35f, 1.0f);
            if (distancia > 1.0f) {
                alvoMao = ombroPulso / distancia * ALCANCE * esticado;
            }
        }
        slot.mao[lado] = slot.dinMao[lado].atualizar(dt, alvoMao);

        const int quadril = usaE ? QUADRIL_E : QUADRIL_D;
        const int joelho = usaE ? JOELHO_E : JOELHO_D;
        const int tornozelo = usaE ? TORNOZELO_E : TORNOZELO_D;
        glm::vec2 perna = glm::normalize(glm::vec2(0.15f * sinal, 1.0f));
        const int fim = visivel(tornozelo) ? tornozelo : (visivel(joelho) ? joelho : -1);
        if (visivel(quadril) && fim >= 0) {
            const glm::vec2 v = glm::normalize(ponto(fim) - ponto(quadril) + glm::vec2(1e-3f, 0));
            perna = glm::normalize(glm::vec2(v.x, std::max(v.y, 0.15f))); // perna de desenho não sobe
        }
        const glm::vec2 p = slot.dinPerna[lado].atualizar(dt, perna);
        slot.perna[lado] = glm::length(p) > 1e-3f ? glm::normalize(p) : perna;
    }

    // ---- Rabo: corrente de Verlet (spring bone) ----
    // Cada elo tem inércia e é puxado para a curva de repouso (rigidez), perde
    // velocidade (arrasto) e mantém o comprimento. Quando o corpo se mexe, o
    // rabo atrasa e chicoteia sozinho.
    const bool gato = estado->animal == Animal::Gato;
    const glm::vec2 base = raizPx + (direita * 0.30f - cima * 0.34f) * uPx;
    const float elo = 0.16f * uPx;
    const float balanco = 0.22f * std::sin(slot.relogioRabo * 2.2f + slot.id);
    auto direcaoRepouso = [&](int k) {
        const float theta = gato ? 0.35f + 0.30f * k + balanco : -0.10f - 0.05f * k + balanco * 0.4f;
        return direita * std::cos(theta) + cima * std::sin(theta);
    };
    if (!slot.raboIniciado) {
        slot.raboIniciado = true;
        glm::vec2 p = base;
        for (int k = 0; k < NUM_ELOS_RABO; ++k) {
            slot.rabo[k] = slot.raboAnterior[k] = p;
            p += direcaoRepouso(k) * elo;
        }
    }
    const float rigidez = 1.0f - std::pow(1.0f - (gato ? 0.18f : 0.28f), dt * 60.0f);
    const float arrasto = std::pow(1.0f - 0.12f, dt * 60.0f);
    slot.rabo[0] = slot.raboAnterior[0] = base;
    for (int k = 1; k < NUM_ELOS_RABO; ++k) {
        glm::vec2& p = slot.rabo[k];
        const glm::vec2 velocidade = (p - slot.raboAnterior[k]) * arrasto;
        slot.raboAnterior[k] = p;
        p += velocidade;
        p.y += 0.4f * uPx * dt * dt * 60.0f; // um pouco de gravidade
        p += (slot.rabo[k - 1] + direcaoRepouso(k - 1) * elo - p) * rigidez;
        const glm::vec2 d = p - slot.rabo[k - 1];
        const float comprimento = glm::length(d);
        p = slot.rabo[k - 1] + (comprimento > 1e-3f ? d / comprimento : direcaoRepouso(k - 1)) * elo;
    }
}

void ofApp::atualizarMusica(float dt) {
    niveis = audio->niveis();
    const float sensibilidade = estado->sensibilidadeSom;
    niveis.grave = std::min(1.0f, niveis.grave * sensibilidade);
    niveis.medio = std::min(1.0f, niveis.medio * sensibilidade);
    niveis.agudo = std::min(1.0f, niveis.agudo * sensibilidade);
    niveis.geral = std::min(1.0f, niveis.geral * sensibilidade);
    const bool houveBatida = niveis.batidas != ultimaBatida;
    ultimaBatida = niveis.batidas;
    if (houveBatida) {
        flash = 1.0f;
        if (estado->efeito == Efeito::Ecos && !historicoMascaras.empty()) {
            // Carimba a silhueta atual: fica parada e some devagar.
            const cv::Mat& atual = historicoMascaras.front();
            carimboTexture.loadData(atual.data, atual.cols, atual.rows, GL_RED);
            carimboForca = 1.0f;
        }
    }
    carimboForca = aproximar(carimboForca, 0.0f, dt, 0.7f);
    if (estado->corSegueMusica) {
        // A cor gira devagar sozinha e mais rápido quanto mais alta a música;
        // cada batida dá um salto de matiz.
        const float velocidadeMatiz = 0.015f + (niveis.ativo ? 0.12f * niveis.geral : 0.0f);
        if (houveBatida) {
            matiz += 0.06f + 0.08f * niveis.grave;
        }
        matiz = std::fmod(matiz + velocidadeMatiz * dt, 1.0f);
    } else {
        matiz = estado->matizFixo;
    }
    flash = aproximar(flash, 0.0f, dt, 0.15f);
}

void ofApp::draw() {
    ofClear(0, 0, 0, 255);
    const std::string estado = processador->estado();
    if (resultado.sequencia == 0) {
        ofSetColor(140);
        ofDrawBitmapString(estado, 30, 40);
        return;
    }
    if (debug) {
        desenharDebug();
    } else {
        desenharEfeitos();
    }
    if (!estado.empty()) {
        // Câmera caiu depois de já ter funcionado: avisa sem apagar a cena.
        ofSetColor(140);
        ofDrawBitmapString(estado, 30, ofGetHeight() - 30);
    }
}

void ofApp::desenharEfeitos() {
    std::array<glm::vec3, config::MAX_PESSOAS * NUM_KEYPOINTS> pontos{};
    std::array<glm::vec4, config::MAX_PESSOAS> pessoas{};
    std::array<glm::vec4, config::MAX_PESSOAS> cabecas{};
    std::array<glm::vec4, config::MAX_PESSOAS> corpos{};
    std::array<glm::vec4, config::MAX_PESSOAS> rig3{};
    std::array<glm::vec2, config::MAX_PESSOAS * NUM_ELOS_RABO> raboPontos{};
    std::array<glm::vec4, config::MAX_PESSOAS> extras{};
    std::array<glm::vec4, config::MAX_PESSOAS> rig1{};
    std::array<glm::vec4, config::MAX_PESSOAS> rig2{};
    std::array<glm::vec4, config::MAX_PESSOAS> bracos{};
    // Personagens bem menores que o maior em cena são detecções falsas
    // (pôster, reflexo): somem.
    float maiorU = 0;
    for (const Slot& slot : slots) {
        if (slot.rigValido > 0.5f) {
            maiorU = std::max(maiorU, slot.rigU);
        }
    }
    for (int s = 0; s < config::MAX_PESSOAS; ++s) {
        const Slot& slot = slots[s];
        pessoas[s] = {slot.presenca, slot.energiaE, slot.energiaD, slot.alturaCorpo};
        cabecas[s] = {slot.cabecaCentro.x, slot.cabecaCentro.y, slot.cabecaRaio, slot.cabecaValida};
        corpos[s] = {slot.cabecaDir.x, slot.cabecaDir.y, slot.escala, slot.orelhaAngulo};
        extras[s] = {slot.pupila.x, slot.pupila.y, slot.piscar, slot.raboAngulo};
        const float relativo = maiorU > 0 ? glm::smoothstep(0.45f, 0.60f, slot.rigU / maiorU) : 1.0f;
        const float anguloCabeca = slot.cabecaInclinacao;
        for (int k = 0; k < NUM_ELOS_RABO; ++k) {
            raboPontos[s * NUM_ELOS_RABO + k] = slot.rabo[k] / glm::vec2(config::CAMERA_LARGURA, config::CAMERA_ALTURA);
        }
        rig1[s] = {slot.rigRaiz.x, slot.rigRaiz.y, slot.rigU, slot.rigValido * relativo};
        rig2[s] = {slot.rigCorpoAngulo, anguloCabeca,
                   std::atan2(slot.perna[0].y, slot.perna[0].x), std::atan2(slot.perna[1].y, slot.perna[1].x)};
        bracos[s] = {slot.mao[0].x, slot.mao[0].y, slot.mao[1].x, slot.mao[1].y};
        rig3[s] = {slot.rigEsticar, slot.cabecaDeslocamento.x, slot.cabecaDeslocamento.y, 0};
        for (int k = 0; k < NUM_KEYPOINTS; ++k) {
            const auto& ponto = slot.pontos[k];
            pontos[s * NUM_KEYPOINTS + k] = {ponto.x, ponto.y, ponto.confianca};
        }
    }

    ofSetColor(255);
    shaderEfeitos.begin();
    shaderEfeitos.setUniformTexture("mascara", mascaraTexture, 0);
    shaderEfeitos.setUniform2f("resolucao", ofGetWidth(), ofGetHeight());
    shaderEfeitos.setUniform1f("tempo", ofGetElapsedTimef());
    shaderEfeitos.setUniform3fv("pontos", &pontos[0].x, static_cast<int>(pontos.size()));
    shaderEfeitos.setUniform4fv("pessoas", &pessoas[0].x, static_cast<int>(pessoas.size()));
    shaderEfeitos.setUniform1f("efeitoMaos", efeitoMaos);
    shaderEfeitos.setUniform1f("efeitoNuvem", efeitoNuvem);
    shaderEfeitos.setUniform1f("efeitoEcos", efeitoEcos);
    shaderEfeitos.setUniformTexture("ecosA", ecosATexture, 1);
    shaderEfeitos.setUniformTexture("ecosB", ecosBTexture, 2);
    shaderEfeitos.setUniformTexture("carimbo", carimboTexture, 3);
    shaderEfeitos.setUniform1f("numEcos", std::round(estado->quantidadeEcos));
    shaderEfeitos.setUniform1f("carimboForca", carimboForca);
    shaderEfeitos.setUniformTexture("quadrinho", quadrinhoTexture, 4);
    shaderEfeitos.setUniform2f("centroCena", centroCena.x, centroCena.y);
    shaderEfeitos.setUniform1f("efeitoBichinho", efeitoBichinho);
    shaderEfeitos.setUniform1f("efeitoFundo", efeitoFundo);
    shaderEfeitos.setUniform1f("animal", static_cast<float>(estado->animal));
    shaderEfeitos.setUniform1f("efeitoLego", efeitoLego);
    shaderEfeitos.setUniform1f("tamanhoPeca", estado->tamanhoPeca);
    shaderEfeitos.setUniform1f("legoCair", estado->legoCair ? 1.0f : 0.0f);
    shaderEfeitos.setUniformTexture("camera", cameraTexture, 5);
    shaderEfeitos.setUniform1f("ganhoCamera", ganhoCamera);
    shaderEfeitos.setUniform1f("brilhoGeral", brilho);
    shaderEfeitos.setUniform4fv("cabeca", &cabecas[0].x, static_cast<int>(cabecas.size()));
    shaderEfeitos.setUniform4fv("corpo", &corpos[0].x, static_cast<int>(corpos.size()));
    shaderEfeitos.setUniform4fv("rig3", &rig3[0].x, static_cast<int>(rig3.size()));
    shaderEfeitos.setUniform2fv("raboPontos", &raboPontos[0].x, static_cast<int>(raboPontos.size()));
    shaderEfeitos.setUniform4fv("extra", &extras[0].x, static_cast<int>(extras.size()));
    shaderEfeitos.setUniform4fv("rig1", &rig1[0].x, static_cast<int>(rig1.size()));
    shaderEfeitos.setUniform4fv("rig2", &rig2[0].x, static_cast<int>(rig2.size()));
    shaderEfeitos.setUniform4fv("bracos", &bracos[0].x, static_cast<int>(bracos.size()));
    shaderEfeitos.setUniform4f("musica", niveis.grave, niveis.medio, niveis.agudo, flash);
    shaderEfeitos.setUniform1f("matiz", matiz);
    quadTela.draw();
    shaderEfeitos.end();
    if (efeitoParaquedas > 0.01f) {
        paraquedistas.desenhar(ofGetWidth(), ofGetHeight(), rostos, efeitoParaquedas);
    }
}



void ofApp::atualizarParaquedistas(float dt) {
    if (estado->efeito != Efeito::Paraquedas && efeitoParaquedas < 0.01f) {
        paraquedistas.limpar();
        return;
    }
    rostos.clear();
    for (int s = 0; s < config::MAX_PESSOAS; ++s) {
        const Slot& slot = slots[s];
        if (slot.presenca < 0.05f || slot.cabecaValida < 0.05f || !rostoPronto[s]) {
            continue;
        }
        rostos.push_back({s, slot.presenca * slot.cabecaValida, &texturasRosto[s], slot.rostoDeFrente});
    }
    paraquedistas.atualizar(dt, rostos, niveis.geral, estado->intervaloAviao);
}

// Recorta o rosto de cada pessoa do quadro da câmera numa imagem 256x256:
// centrado entre os olhos e o nariz, tamanho pela distância entre os olhos,
// girado para ficar reto, luz corrigida (CLAHE no canal L do Lab, bom para
// rosto escuro ou contraluz) e uma leve nitidez.
void ofApp::recortarRostos(const cv::Mat& quadro) {
    constexpr int LADO = 256;
    constexpr float L = config::CAMERA_LARGURA;
    constexpr float A = config::CAMERA_ALTURA;
    if (!clahe) {
        clahe = cv::createCLAHE(2.5, cv::Size(4, 4));
    }
    for (int s = 0; s < config::MAX_PESSOAS; ++s) {
        Slot& slot = slots[s];
        if (slot.presenca < 0.05f || slot.cabecaValida < 0.05f) {
            rostoPronto[s] = false;
            slot.rostoIniciado = false;
            continue;
        }
        auto ponto = [&](int k) { return glm::vec2(slot.pontos[k].x * L, slot.pontos[k].y * A); };
        auto visivel = [&](int k) { return slot.pontos[k].confianca > 0.3f; };

        // Só atualiza o rosto de frente (olhos e nariz visíveis); de lado ou de
        // costas o recorte pegaria nuca/cabelo, então fica o último rosto bom.
        slot.rostoDeFrente = slot.pontos[OLHO_E].confianca > 0.5f && slot.pontos[OLHO_D].confianca > 0.5f
            && slot.pontos[NARIZ].confianca > 0.5f;
        if (!slot.rostoDeFrente) {
            continue;
        }
        glm::vec2 centro;
        float lado;
        if (visivel(OLHO_E) && visivel(OLHO_D)) {
            const glm::vec2 meioOlhos = 0.5f * (ponto(OLHO_E) + ponto(OLHO_D));
            const float distancia = std::max(glm::length(ponto(OLHO_D) - ponto(OLHO_E)), 6.0f);
            const glm::vec2 baixo(-slot.cabecaDir.y, slot.cabecaDir.x); // perpendicular aos olhos, para o queixo
            centro = visivel(NARIZ) ? meioOlhos + (ponto(NARIZ) - meioOlhos) * 0.45f : meioOlhos + baixo * distancia * 0.35f;
            lado = 3.4f * distancia;
        } else {
            centro = glm::vec2(slot.cabecaCentro.x * L, slot.cabecaCentro.y * A);
            lado = slot.cabecaRaio * A * 2.6f;
        }
        lado = std::max(lado, 24.0f);
        const float angulo = glm::degrees(std::atan2(slot.cabecaDir.y, slot.cabecaDir.x));
        if (!slot.rostoIniciado) {
            slot.rostoIniciado = true;
            slot.rostoCentro = centro;
            slot.rostoLado = lado;
            slot.rostoAngulo = angulo;
        } else {
            // Suaviza o enquadramento para o rosto não "tremer" no recorte.
            slot.rostoCentro += (centro - slot.rostoCentro) * 0.5f;
            slot.rostoLado += (lado - slot.rostoLado) * 0.3f;
            slot.rostoAngulo += (angulo - slot.rostoAngulo) * 0.4f;
        }

        cv::Mat matriz = cv::getRotationMatrix2D(
            cv::Point2f(slot.rostoCentro.x, slot.rostoCentro.y), slot.rostoAngulo, LADO / slot.rostoLado
        );
        matriz.at<double>(0, 2) += LADO / 2.0 - slot.rostoCentro.x;
        matriz.at<double>(1, 2) += LADO / 2.0 - slot.rostoCentro.y;
        cv::Mat rosto;
        cv::warpAffine(quadro, rosto, matriz, cv::Size(LADO, LADO), cv::INTER_LINEAR, cv::BORDER_REPLICATE);

        cv::Mat lab;
        cv::cvtColor(rosto, lab, cv::COLOR_BGR2Lab);
        std::vector<cv::Mat> canais;
        cv::split(lab, canais);
        clahe->apply(canais[0], canais[0]);
        cv::merge(canais, lab);
        cv::cvtColor(lab, rosto, cv::COLOR_Lab2BGR);
        cv::Mat borrado;
        cv::GaussianBlur(rosto, borrado, cv::Size(), 1.2);
        cv::addWeighted(rosto, 1.5, borrado, -0.5, 0, rosto);
        cv::cvtColor(rosto, rosto, cv::COLOR_BGR2RGB);

        if (!texturasRosto[s].isAllocated()) {
            texturasRosto[s].allocate(LADO, LADO, GL_RGB8);
            texturasRosto[s].setTextureWrap(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
        }
        texturasRosto[s].loadData(rosto.data, LADO, LADO, GL_RGB);
        rostoPronto[s] = true;
    }
}

// Centro da pessoa principal (maior presença), para os raios do fundo pop-art.
void ofApp::atualizarCentroCena(float dt) {
    float melhor = 0;
    for (const Slot& slot : slots) {
        if (slot.presenca > melhor && slot.cabecaValida > 0.3f) {
            melhor = slot.presenca;
            centroCena += (slot.cabecaCentro - centroCena) * (1.0f - std::exp(-dt / 0.3f));
        }
    }
}

void ofApp::desenharDebug() {
    const float largura = ofGetWidth();
    const float altura = ofGetHeight();
    ofSetColor(110);
    cameraTexture.draw(0, 0, largura, altura);
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    desenharEfeitos();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);

    const float sx = largura / config::CAMERA_LARGURA;
    const float sy = altura / config::CAMERA_ALTURA;
    ofNoFill();
    for (const auto& pessoa : resultado.pessoas) {
        ofSetColor(pessoa.ativa ? ofColor(255, 220, 0) : ofColor(120, 120, 120));
        ofDrawRectangle(pessoa.caixa.x * sx, pessoa.caixa.y * sy, pessoa.caixa.width * sx, pessoa.caixa.height * sy);
        ofDrawBitmapStringHighlight(
            "id " + ofToString(pessoa.id) + " slot " + ofToString(pessoa.slot)
                + " maos " + ofToString(pessoa.velocidadePulsoE, 1) + "/" + ofToString(pessoa.velocidadePulsoD, 1),
            pessoa.caixa.x * sx + 4, pessoa.caixa.y * sy + 14
        );
        ofSetColor(255, 60, 60);
        for (const auto& [a, b] : OSSOS_DEBUG) {
            const auto& pa = pessoa.pontos[a];
            const auto& pb = pessoa.pontos[b];
            if (pa.confianca > 0.2f && pb.confianca > 0.2f) {
                ofDrawLine(pa.x * sx, pa.y * sy, pb.x * sx, pb.y * sy);
            }
        }
    }
    ofFill();

    std::ostringstream hud;
    hud << std::fixed << std::setprecision(1)
        << "render " << ofGetFrameRate() << " fps | visao " << resultado.quadroMs << " ms ("
        << (resultado.usandoCuda ? "CUDA" : "CPU") << ")\n"
        << "pre " << resultado.tempos.preprocessamentoMs
        << "  seg " << resultado.tempos.segmentacaoMs
        << "  pose " << resultado.tempos.poseMs
        << "  pos " << resultado.tempos.posprocessamentoMs
        << "  mascara " << resultado.composicaoMs << " ms\n"
        << "pessoas " << resultado.pessoas.size()
        << " | [1] luz [2] bichinho | orbes " << estado->orbesMaos << "\n"
        << "audio " << (niveis.ativo ? "ok" : "silencio") << std::setprecision(2)
        << "  grave " << niveis.grave << "  medio " << niveis.medio << "  agudo " << niveis.agudo
        << "  batidas " << niveis.batidas << "  matiz " << matiz;
    ofDrawBitmapStringHighlight(hud.str(), 20, 24);
}

void ofApp::keyPressed(int key) {
    if (key == 'f' || key == 'F') {
        ofToggleFullscreen();
        atualizarCursor();
    } else if (key == 'q' || key == 'Q' || key == OF_KEY_ESC) {
        ofExit();
    } else if (key == 'd' || key == 'D') {
        debug = !debug;
        processador->definirDebug(debug);
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

void ofApp::atualizarCursor() {
    // Em tela cheia (projetor) o cursor não pode aparecer na projeção.
    if (ofGetWindowMode() == OF_FULLSCREEN) {
        ofHideCursor();
    } else {
        ofShowCursor();
    }
}

void ofApp::windowResized(int w, int h) {
    atualizarQuad(w, h);
}

void ofApp::exit() {
    estado->encerrar = true; // o painel fecha junto
    if (processador) {
        processador->parar();
    }
    if (audio) {
        audio->parar();
    }
}

void ofApp::atualizarQuad(int largura, int altura) {
    quadTela.clear();
    quadTela.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
    quadTela.addVertex({0, 0, 0});
    quadTela.addTexCoord({0, 0});
    quadTela.addVertex({static_cast<float>(largura), 0, 0});
    quadTela.addTexCoord({1, 0});
    quadTela.addVertex({static_cast<float>(largura), static_cast<float>(altura), 0});
    quadTela.addTexCoord({1, 1});
    quadTela.addVertex({0, static_cast<float>(altura), 0});
    quadTela.addTexCoord({0, 1});
}

void ofApp::criarShader() {
    shaderEfeitos.setupShaderFromSource(GL_VERTEX_SHADER, VERTEX_SHADER);
    shaderEfeitos.setupShaderFromSource(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
    shaderEfeitos.bindDefaults();
    shaderEfeitos.linkProgram();
}
