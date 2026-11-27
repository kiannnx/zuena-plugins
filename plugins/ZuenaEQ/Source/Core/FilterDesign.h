#pragma once
// Diseño de filtros con ajuste analógico de alta precisión y parametrización TPT SVF
// (Trapezoidal State Variable Filter, A. Simper / Cytomic).
//
// - Polos por invariancia al impulso (matched-z) y numerador ajustado exactamente en DC,
//   frecuencia característica y Nyquist con relajación de amortiguamiento mínimo (M. Vicanek, 2016).
// - Cortes altos (Low Cut) con doble cero exacto en DC; Tilt simétrico autoinverso; Flat Tilt
//   de pendiente constante en 4 décadas; All Pass de módulo unitario exacto.
// - Conversión isomorfa exacta a integradores trapezoidales (SvfSection) para máxima precisión
//   en bajas frecuencias y estabilidad garantizada bajo modulación rápida.

#include "ZeqTypes.h"

#include <array>
#include <complex>
#include <span>

namespace zeq
{
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct Design
{
    std::array<Biquad, kMaxSections> sections {};
    int count = 0;
};

/** Coeficientes de una sección en topología TPT State Variable Filter (1.º o 2.º orden). */
struct SvfSection
{
    int order = 2;              // 1: integrador TPT de 1 polo; 2: SVF trapezoidal de 2 polos
    double g = 0.0;             // ganancia del integrador trapezoidal
    double k = 1.0;             // amortiguamiento (2 * zeta)
    double a1 = 0.0, a2 = 0.0, a3 = 0.0;  // factores precalculados para estado estacionario
    double chp = 1.0, cbp = 0.0, clp = 0.0;
};

struct SvfDesign
{
    std::array<SvfSection, kMaxSections> sections {};
    int count = 0;
};

/** Diseña la cascada de una banda. Siempre devuelve secciones estables (polos dentro del círculo unidad). */
Design designBand (FilterType type, double freqHz, double gainDb, double q, int slopeDbPerOct, double sampleRate) noexcept;

/** Filtro paso banda de 2.º orden (detector de la dinámica y audición en solo). */
Biquad designBandPass (double freqHz, double q, double sampleRate) noexcept;

/** Convierte de forma exacta una sección o cascada Biquad a topología TPT SVF. */
SvfSection toSvf (const Biquad& b) noexcept;
SvfDesign toSvf (const Design& d) noexcept;

/** Respuesta compleja de la cascada a la frecuencia dada. */
std::complex<double> response (const Design& d, double freqHz, double sampleRate) noexcept;
std::complex<double> response (const Biquad& s, double freqHz, double sampleRate) noexcept;

/** Fase en grados (-180 a +180) de una cascada a la frecuencia dada. */
double phaseDegrees (const Design& d, double freqHz, double sampleRate) noexcept;

/** Estimación perceptual ponderada en ruido rosa (1/f, 30 Hz - 16 kHz) para compensar Auto-Gain (dB). */
double computeAutoGainDb (std::span<const BandParams> bands, double sampleRate) noexcept;

/** Módulo en dB del prototipo analógico de referencia (para tests y comparación). */
double analogMagnitudeDb (FilterType type, double freqHz, double gainDb, double q, int slopeDbPerOct, double evalHz) noexcept;
} // namespace zeq
