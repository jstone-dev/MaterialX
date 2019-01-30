#ifndef MATERIALXVIEW_VIEWER_H
#define MATERIALXVIEW_VIEWER_H

#include <MaterialXView/Editor.h>
#include <MaterialXView/Material.h>
#include <MaterialXRender/Handlers/GeometryHandler.h>

namespace mx = MaterialX;
namespace ng = nanogui;

class Viewer : public ng::Screen
{
  public:
    Viewer(const mx::StringVec& libraryFolders,
           const mx::FileSearchPath& searchPath,
           const mx::StringMap& nodeRemap,
           int multiSampleCount);
    ~Viewer() { }

    void drawContents() override;
    bool keyboardEvent(int key, int scancode, int action, int modifiers) override;

    bool scrollEvent(const ng::Vector2i& p, const ng::Vector2f& rel) override;
    bool mouseMotionEvent(const ng::Vector2i& p, const ng::Vector2i& rel, int button, int modifiers) override;
    bool mouseButtonEvent(const ng::Vector2i& p, int button, bool down, int modifiers) override;

    ng::Window* getWindow() const
    {
        return _window;
    }

    MaterialPtr getCurrentMaterial() const
    {
        return _materials[_geomIndex];
    }

    mx::DocumentPtr getCurrentDocument() const
    {
        return getCurrentMaterial()->getDocument();
    }

    const mx::FileSearchPath& getSearchPath() const
    {
        return _searchPath;
    }

    const mx::GLTextureHandlerPtr getImageHandler() const
    {
        return _imageHandler;
    }

  private:
    void initCamera();
    void computeCameraMatrices(mx::Matrix44& world,
                               mx::Matrix44& view,
                               mx::Matrix44& proj);

    bool setGeometrySelection(size_t index);
    void updateGeometrySelections();

    bool setSubsetSelection(size_t index);
    void updateSubsetSelections();

    void updatePropertyEditor();

  private:
    ng::Window* _window;
    ng::Arcball _arcball;
    PropertyEditor _propertyEditor;

    mx::GeometryHandler _geometryHandler;
    std::vector<MaterialPtr> _materials;

    mx::Vector3 _eye;
    mx::Vector3 _center;
    mx::Vector3 _up;
    float _zoom;
    float _viewAngle;
    float _nearDist;
    float _farDist;

    mx::Vector3 _modelTranslation;
    mx::Vector3 _modelTranslationStart;
    float _modelZoom;

    bool _translationActive;
    ng::Vector2i _translationStart;

    mx::StringVec _libraryFolders;
    mx::FileSearchPath _searchPath;
    mx::StringMap _nodeRemap;

    mx::FilePath _materialFilename;
    int _envSamples;

    mx::DocumentPtr _stdLib;

    ng::Label* _geomLabel;
    ng::ComboBox* _geomSelectionBox;
    std::vector<mx::MeshPartitionPtr> _geomSelections;
    size_t _geomIndex;

    ng::Label* _materialLabel;
    ng::ComboBox* _subsetSelectionBox;

    mx::GLTextureHandlerPtr _imageHandler;
};

#endif // MATERIALXVIEW_VIEWER_H
