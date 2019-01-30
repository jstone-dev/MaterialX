#ifndef MATERIALXVIEW_MATERIAL_H
#define MATERIALXVIEW_MATERIAL_H

#include <MaterialXRender/Handlers/GeometryHandler.h>

#include <MaterialXCore/Document.h>
#include <MaterialXFormat/XmlIo.h>
#include <MaterialXFormat/File.h>

#include <MaterialXRender/OpenGL/GLTextureHandler.h>
#include <MaterialXGenShader/HwShader.h>

#include <nanogui/common.h>
#include <nanogui/glutil.h>

namespace mx = MaterialX;
namespace ng = nanogui;

using MaterialPtr = std::shared_ptr<class Material>;
using GLShaderPtr = std::shared_ptr<ng::GLShader>;

using StringPair = std::pair<std::string, std::string>;

class MaterialSubset
{
  public:
    mx::TypedElementPtr elem;
    std::string udim;
};

class Material
{
  public:
    Material() :
        _subsetIndex(0)
    {
    }
    ~Material() { }

    /// Load a new content document.
    void loadDocument(const mx::FilePath& filePath, mx::DocumentPtr stdLib, const mx::StringMap& nodeRemap);

    /// Return the content document.
    mx::DocumentPtr getDocument()
    {
        return _doc;
    }

    /// Return the vector of material subsets.
    const std::vector<MaterialSubset>& getSubsets()
    {
        return _subsets;
    }

    /// Return the current material subset.
    const MaterialSubset& getCurrentSubset()
    {
        return _subsets[_subsetIndex];
    }

    /// Set the current material subset index.
    void setSubsetIndex(size_t index)
    {
        _subsetIndex = index;
    }

    /// Return the current material subset index.
    size_t getSubsetIndex()
    {
        return _subsetIndex;
    }

    /// Generate a shader from the given inputs.
    bool generateShader(const mx::FileSearchPath& searchPath, mx::ElementPtr elem);

    /// Return the underlying OpenGL shader.
    GLShaderPtr getShader() const
    {
        return _glShader;
    }

    /// Return true if this material has transparency.
    bool hasTransparency() const
    {
        return _hasTransparency;
    }
    
    /// Bind viewing information for this material.
    void bindViewInformation(const mx::Matrix44& world, const mx::Matrix44& view, const mx::Matrix44& proj);

    /// Bind all images for this material.
    void bindImages(mx::GLTextureHandlerPtr imageHandler, const mx::FileSearchPath& imagePath, const std::string& udim);

    /// Bind a single image.
    bool bindImage(std::string filename, const std::string& uniformName, mx::GLTextureHandlerPtr imageHandler,
        mx::ImageDesc& desc, const std::string& udim = mx::EMPTY_STRING);

    /// Bind lights to shader.
    void bindLights(mx::GLTextureHandlerPtr imageHandler, const mx::FileSearchPath& imagePath, int envSamples);

    /// Bind the given mesh to this material.
    void bindMesh(mx::MeshPtr mesh) const;

    /// Bind a mesh partition to this material.
    void bindPartition(mx::MeshPartitionPtr part) const;

    /// Draw the given mesh partition.
    void drawPartition(mx::MeshPartitionPtr part) const;

    // Return the block of public uniforms for this material.
    const mx::Shader::VariableBlock* getPublicUniforms() const;

    /// Find a public uniform from its MaterialX path.
    mx::Shader::Variable* findUniform(const std::string& path) const;

  protected:
    mx::DocumentPtr _doc;
    GLShaderPtr _glShader;
    mx::HwShaderPtr _hwShader;
    std::vector<MaterialSubset> _subsets;
    size_t _subsetIndex;
    bool _hasTransparency;
};

mx::DocumentPtr loadLibraries(const mx::StringVec& libraryNames, const mx::FileSearchPath& searchPath);
mx::HwShaderPtr generateSource(const mx::FileSearchPath& searchPath, mx::ElementPtr elem);

#endif // MATERIALXVIEW_MATERIAL_H
