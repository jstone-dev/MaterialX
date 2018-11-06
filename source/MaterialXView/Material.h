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

using ViewerShaderPtr = std::unique_ptr<class ViewerShader>;

class ViewerShader
{
  public:
    static ViewerShaderPtr generateShader(const mx::FilePath& filePath, const mx::FilePath& searchPath, mx::DocumentPtr stdLib);

    GLShaderPtr ngShader() const { return _ngShader; }
    mx::HwShaderPtr mxShader() const { return _mxShader; }

    void bindMesh(MeshPtr& mesh);
    void bindUniforms(mx::ImageHandlerPtr imageHandler, mx::FilePath imagePath, int envSamples,
                      mx::Matrix44& world, mx::Matrix44& view, mx::Matrix44& proj);

    bool isTransparent() const { return _isTransparent; }

  protected:
    ViewerShader(GLShaderPtr ngshader, mx::HwShaderPtr mxshader)
    {
        _ngShader = ngshader;
        _mxShader = mxshader;
    }

    GLShaderPtr _ngShader;
    mx::HwShaderPtr _mxShader;
    bool _isTransparent;
};

void loadLibraries(const mx::StringVec& libraryNames, const mx::FilePath& searchPath, mx::DocumentPtr doc);

StringPair generateSource(const mx::FilePath& filePath, const mx::FilePath& searchPath, mx::DocumentPtr stdLib, mx::HwShaderPtr& hwShader);

#endif // MATERIALXVIEW_MATERIAL_H
