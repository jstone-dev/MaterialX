#ifndef MATERIALXVIEW_MATERIAL_H
#define MATERIALXVIEW_MATERIAL_H

#include <MaterialXView/Mesh.h>

#include <MaterialXCore/Document.h>
#include <MaterialXFormat/XmlIo.h>
#include <MaterialXFormat/File.h>

#include <MaterialXRender/Handlers/TinyExrImageHandler.h>
#include <MaterialXGenShader/HwShader.h>

#include <nanogui/common.h>
#include <nanogui/glutil.h>

namespace mx = MaterialX;
namespace ng = nanogui;

using StringPair = std::pair<std::string, std::string>;
using GLShaderPtr = std::shared_ptr<ng::GLShader>;

using MaterialPtr = std::unique_ptr<class Material>;

// TODO: Move image caching into the ImageHandler class.
class ImageDesc
{
public:
    unsigned int width = 0; // TODO: These would be better as size_t.
    unsigned int height = 0;
    unsigned int channelCount = 0;
    unsigned int mipCount = 0;
    unsigned int textureId = 0;
};

class Material
{
  public:
    static MaterialPtr generateShader(const mx::FilePath& filePath, const mx::FilePath& searchPath, mx::DocumentPtr stdLib);

    /// Get Nano-gui shader
    GLShaderPtr ngShader() const { return _ngShader; }
    
    /// Get MaterialX shader
    mx::HwShaderPtr mxShader() const { return _mxShader; }

    /// Bind mesh inputs to shader
    void bindMesh(MeshPtr& mesh);

    /// Bind uniforms to shader
    void bindUniforms(mx::ImageHandlerPtr imageHandler, mx::FilePath imagePath, int envSamples,
                      mx::Matrix44& world, mx::Matrix44& view, mx::Matrix44& proj);

    /// Bind texture to shader
    bool bindTexture(const std::string& fileName, const std::string& uniformName,
                     mx::ImageHandlerPtr imageHandler, ImageDesc& desc);

    /// Bind required file textures to shader
    void Material::bindTextures(mx::ImageHandlerPtr imageHandler);

    /// Return if the shader is has transparency
    bool hasTransparency() const { return _hasTransparency; }

  protected:
    Material(GLShaderPtr ngshader, mx::HwShaderPtr mxshader)
    {
        _ngShader = ngshader;
        _mxShader = mxshader;
    }

    // Acquire a texture. Return information in image description
    bool acquireTexture(const std::string& filename, mx::ImageHandlerPtr imageHandler, ImageDesc& desc);

    GLShaderPtr _ngShader;
    mx::HwShaderPtr _mxShader;
    bool _hasTransparency;
};

void loadLibraries(const mx::StringVec& libraryNames, const mx::FilePath& searchPath, mx::DocumentPtr doc);

StringPair generateSource(const mx::FilePath& filePath, const mx::FilePath& searchPath, mx::DocumentPtr stdLib, mx::HwShaderPtr& hwShader);

#endif // MATERIALXVIEW_MATERIAL_H
