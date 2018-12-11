void sx_roughness_anisotropy(float roughness, float anisotropy, out roughnessinfo result)
{
    result.roughness = roughness;
    result.alpha = clamp(roughness*roughness, M_FLOAT_EPS, 1.0);
    if (anisotropy > 0.0)
    {
        float aspect = sqrt(1.0 - clamp(anisotropy, 0.0, 0.98));
        result.alphaX = min(result.alpha / aspect, 1.0);
        result.alphaY = result.alpha * aspect;
    }
    else
    {
        result.alphaX = result.alpha;
        result.alphaY = result.alpha;
    }
}

void sx_roughness_dual(float roughnessX, float roughnessY, out roughnessinfo result)
{
    if (roughnessY < 0.0)
    {
        roughnessY = roughnessX;
    }
    result.roughness = sqrt(roughnessX * roughnessY);
    result.alpha = clamp(result.roughness * result.roughness, M_FLOAT_EPS, 1.0);
    result.alphaX = clamp(roughnessX * roughnessX, M_FLOAT_EPS, 1.0);
    result.alphaY = clamp(roughnessY * roughnessY, M_FLOAT_EPS, 1.0);
}
