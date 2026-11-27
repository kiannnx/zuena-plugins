#pragma once
// Primitivas de animación sin reservas dinámicas (Prisma Motion de ZUENA: amortiguación suave y resorte).
// Independientes de la tasa de fotogramas (60 / 120 / 144 Hz).

#include <algorithm>
#include <cmath>

namespace zeq::ui::motion
{
/** Amortiguación exponencial independiente de dt (equivalente a ease-out continuo). */
inline float damp (float current, float target, float speed, float dt) noexcept
{
    dt = std::clamp (dt, 0.001f, 0.05f);
    const float next = current + (target - current) * (1.0f - std::exp (-speed * dt));
    return std::abs (target - next) < 1.0e-4f ? target : next;
}

/** Resorte de 2.º orden (Prisma Motion: entrada viva sin rebote molesto). */
struct Spring
{
    float value = 1.0f;
    float velocity = 0.0f;
    float target = 1.0f;

    void snap (float v) noexcept
    {
        value = target = v;
        velocity = 0.0f;
    }

    float step (float dt, float stiffness = 320.0f, float damping = 26.0f) noexcept
    {
        dt = std::clamp (dt, 0.001f, 0.033f);
        // Dos sub-pasos de Euler semi-implícito garantizan estabilidad a cualquier FPS.
        const float h = dt * 0.5f;
        for (int i = 0; i < 2; ++i)
        {
            const float force = (target - value) * stiffness - velocity * damping;
            velocity += force * h;
            value += velocity * h;
        }
        if (std::abs (target - value) < 5.0e-4f && std::abs (velocity) < 5.0e-3f)
        {
            value = target;
            velocity = 0.0f;
        }
        return value;
    }
};
} // namespace zeq::ui::motion
