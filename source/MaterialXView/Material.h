#ifndef MATERIALXVIEW_MATERIAL_H
#define MATERIALXVIEW_MATERIAL_H

#include <MaterialXView/Mesh.h>

#include <MaterialXCore/Document.h>
#include <MaterialXFormat/XmlIo.h>
#include <MaterialXFormat/File.h>

#include <MaterialXRender/OpenGL/GLTextureHandler.h>
#include <MaterialXGenShader/HwShader.h>

#include <nanogui/common.h>
#include <nanogui/glutil.h>

namespace mx = MaterialX;
namespace ng = nanogui;

using StringPair = std::pair<std::string, std::string>;
using GLShaderPtr = std::shared_ptr<ng::GLShader>;

using MaterialPtr = std::unique_ptr<class Material>;

class Material
{
  public:
    static MaterialPtr generateShader(const mx::FilePath& searchPath, mx::ElementPtr elem);

    /// Get Nano-gui shader
    GLShaderPtr ngShader() const { return _ngShader; }
    
    /// Get MaterialX shader
    mx::HwShaderPtr mxShader() const { return _mxShader; }

    /// Bind mesh inputs to shader
    void bindMesh(MeshPtr& mesh);

    /// Bind uniforms to shader
    void bindUniforms(mx::GLTextureHandlerPtr imageHandler, mx::FilePath imagePath, int envSamples,
                      mx::Matrix44& world, mx::Matrix44& view, mx::Matrix44& proj);

    /// Bind texture to shader
    bool bindTexture(const std::string& fileName, const std::string& uniformName,
                     mx::GLTextureHandlerPtr imageHandler, mx::ImageDesc& desc);

    /// Bind required file textures to shader
    void bindTextures(mx::GLTextureHandlerPtr imageHandler, mx::FilePath imagePath);

    /// Return if the shader is has transparency
    bool hasTransparency() const { return _hasTransparency; }

  protected:
    Material(GLShaderPtr ngshader, mx::HwShaderPtr mxshader)
    {
        _ngShader = ngshader;
        _mxShader = mxshader;
    }

    // Acquire a texture. Return information in image description
    bool acquireTexture(const std::string& filename, mx::GLTextureHandlerPtr imageHandler, mx::ImageDesc& desc);

    GLShaderPtr _ngShader;
    mx::HwShaderPtr _mxShader;
    bool _hasTransparency;
};

void loadLibraries(const mx::StringVec& libraryNames, const mx::FilePath& searchPath, mx::DocumentPtr doc);
void loadDocument(const mx::FilePath& filePath, mx::DocumentPtr& document, mx::DocumentPtr stdLib, std::vector<mx::TypedElementPtr>& elements);

StringPair generateSource(const mx::FilePath& searchPath, mx::HwShaderPtr& hwShader, mx::ElementPtr elem);

#endif // MATERIALXVIEW_MATERIAL_H
