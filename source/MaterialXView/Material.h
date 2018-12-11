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
    static MaterialPtr generateShader(const mx::FileSearchPath& searchPath, mx::ElementPtr elem);

    /// Get Nano-gui shader
    GLShaderPtr ngShader() const { return _ngShader; }
    
    /// Get MaterialX shader
    mx::HwShaderPtr mxShader() const { return _mxShader; }

    /// Bind mesh inputs to shader
    void bindMesh(MeshPtr& mesh);

    /// Bind viewing information to shader
    void bindViewInformation(const mx::Matrix44& world, const mx::Matrix44& view, const mx::Matrix44& proj);

    /// Bind image to shader
    bool bindImage(const std::string& filename, const std::string& uniformName,
                   mx::GLTextureHandlerPtr imageHandler, mx::ImageDesc& desc);

    /// Bind required images to shader
    void bindImages(mx::GLTextureHandlerPtr imageHandler, const mx::FileSearchPath& imagePath);

    /// Bind lights to shader
    void bindLights(mx::GLTextureHandlerPtr imageHandler, const mx::FileSearchPath& imagePath, int envSamples);

    /// Return if the shader is has transparency
    bool hasTransparency() const { return _hasTransparency; }

    /// Find a variable (uniform) based on MaterialX path
    mx::Shader::Variable* findUniform(const std::string path) const;

  protected:
    Material(GLShaderPtr ngshader, mx::HwShaderPtr mxshader)
    {
        _ngShader = ngshader;
        _mxShader = mxshader;
    }

    GLShaderPtr _ngShader;
    mx::HwShaderPtr _mxShader;
    bool _hasTransparency;
};

void loadLibraries(const mx::StringVec& libraryNames, const mx::FileSearchPath& searchPath, mx::DocumentPtr doc);
void loadDocument(const mx::FilePath& filePath, mx::DocumentPtr& document, mx::DocumentPtr stdLib, std::vector<mx::TypedElementPtr>& elements);
void remapNodes(mx::DocumentPtr& doc, const mx::StringMap& nodeRemap);

StringPair generateSource(const mx::FileSearchPath& searchPath, mx::HwShaderPtr& hwShader, mx::ElementPtr elem);

#endif // MATERIALXVIEW_MATERIAL_H
