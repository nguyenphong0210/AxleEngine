#ifndef AXLE_PROCEDURAL_CHECKER_INCLUDED
#define AXLE_PROCEDURAL_CHECKER_INCLUDED

float CheckerStripeAverage(float coordinate, float width) {
    // The triangle wave is the integral of alternating +1 and -1 stripes.
    // Reduce the phase before evaluating its endpoints to keep small footprints precise.
    float phase = frac(coordinate * 0.5) * 2.0;
    if (width < 1e-4) return phase < 1.0 ? 1.0 : -1.0;
    float low = frac((phase - width * 0.5) * 0.5) * 2.0;
    float high = frac((phase + width * 0.5) * 0.5) * 2.0;
    return clamp((abs(low - 1.0) - abs(high - 1.0)) / width, -1.0, 1.0);
}

float FilterWorldChecker(float2 coordinate) {
    float2 width = fwidth(coordinate);
    return 0.5 - 0.5 * CheckerStripeAverage(coordinate.x, width.x) *
                       CheckerStripeAverage(coordinate.y, width.y);
}

float FilterLocalChecker(float3 coordinate) {
    float3 width = fwidth(coordinate);
    return 0.5 - 0.5 * CheckerStripeAverage(coordinate.x, width.x) *
                       CheckerStripeAverage(coordinate.y, width.y) *
                       CheckerStripeAverage(coordinate.z, width.z);
}

#endif
