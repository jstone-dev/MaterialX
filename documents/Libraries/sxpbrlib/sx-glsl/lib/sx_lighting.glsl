#include "sxpbrlib/sx-glsl/lib/sx_bsdfs.glsl"

vec2 sx_latlong_projection(vec3 dir)
{
    float latitude = -asin(dir.y) * M_PI_INV + 0.5;
    latitude = clamp(latitude, 0.01, 0.99);
    float longitude = atan(dir.x, -dir.z) * M_PI_INV * 0.5 + 0.5;
    return vec2(longitude, latitude);
}

// https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch20.html
// Section 20.4 Equation 13
float sx_latlong_compute_lod(vec3 dir, float pdf, float maxMipLevel, int u_envSamples)
{
    const float MIP_LEVEL_OFFSET = 1.5;
    float effectiveMaxMipLevel = maxMipLevel - MIP_LEVEL_OFFSET;
    float distortion = sqrt(1.0 - sx_square(dir.y));
    return max(effectiveMaxMipLevel - 0.5 * log2(u_envSamples * pdf * distortion), 0.0);
}

vec3 sx_latlong_map_lookup(vec3 dir, mat4 transform, float lod, sampler2D sampler)
{
    vec3 envDir = normalize((transform * vec4(dir,0.0)).xyz);
    vec2 uv = sx_latlong_projection(envDir);
    return textureLod(sampler, uv, lod).rgb;
}

vec3 sx_environment_specular(vec3 N, vec3 V, vec3 X, roughnessinfo roughness)
{
    vec3 Y = normalize(cross(N, X));
    X = cross(Y, N);

    // Compute shared dot products.
    float NdotV = clamp(dot(N, V), 1e-8, 1.0);
    
    // Integrate outgoing radiance using filtered importance sampling.
    // http://cgg.mff.cuni.cz/~jaroslav/papers/2008-egsr-fis/2008-egsr-fis-final-embedded.pdf
    vec3 radiance = vec3(0.0);
    for (int i = 0; i < u_envSamples; i++)
    {
        vec2 Xi = sx_spherical_fibonacci(i, u_envSamples);

        // Compute the half vector and incoming light direction.
        vec3 H = sx_microfacet_ggx_IS(Xi, X, Y, N, roughness.alphaX, roughness.alphaY);
        vec3 L = -reflect(V, H);
        
        // Compute dot products for this sample.
        float NdotH = clamp(dot(N, H), 1e-8, 1.0);
        float NdotL = clamp(dot(N, L), 1e-8, 1.0);
        float VdotH = clamp(dot(V, H), 1e-8, 1.0);
        float LdotH = VdotH;

        // Sample the environment light from the given direction.
        float pdf = sx_microfacet_ggx_PDF(X, Y, H, NdotH, LdotH, roughness.alphaX, roughness.alphaY);
        float lod = sx_latlong_compute_lod(L, pdf, u_envRadianceMips - 1, u_envSamples);
        vec3 sampleColor = sx_latlong_map_lookup(L, u_envMatrix, lod, u_envRadiance);

        // Compute the geometric term.
        float G = sx_microfacet_ggx_smith_G(NdotL, NdotV, roughness.alpha);

        // Add the radiance contribution of this sample.
        // TODO: Move Fresnel term into the lighting integral.
        radiance += sampleColor * NdotL * G / (NdotH / (4.0 * VdotH));
    }

    // Normalize and return the final radiance.
    radiance /= float(u_envSamples);
    return radiance;
}

vec3 sx_environment_irradiance(vec3 N)
{
    return sx_latlong_map_lookup(N, u_envMatrix, 0.0, u_envIrradiance);
}
