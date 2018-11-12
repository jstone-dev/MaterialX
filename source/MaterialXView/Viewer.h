#ifndef MATERIALXVIEW_VIEWER_H
#define MATERIALXVIEW_VIEWER_H

#include <MaterialXView/Material.h>
#include <MaterialXView/Mesh.h>
#include <MaterialXGenShader/HwShader.h>

#include <nanogui/glutil.h>
#include <nanogui/screen.h>
#include <nanogui/formhelper.h>

namespace mx = MaterialX;
namespace ng = nanogui;

class CameraParameters
{
  public:
    ng::Arcball arcball;
    float zoom = 1.0f;
    float viewAngle = 45.0f;
    float dnear = 0.05f;
    float dfar = 100.0f;
    mx::Vector3 eye = { 0.0f, 0.0f, 5.0f };
    mx::Vector3 center = { 0.0f, 0.0f, 0.0f };
    mx::Vector3 up = { 0.0f, 1.0f, 0.0f };
    mx::Vector3 modelTranslation = { 0.0f, 0.0f, 0.0f };
    mx::Vector3 modelTranslationStart = { 0.0f, 0.0f, 0.0f };
    float modelZoom = 1.0f;
};

class Viewer : public ng::Screen
{
  public:
    Viewer();
    ~Viewer() { };

    void drawContents() override;
    bool keyboardEvent(int key, int scancode, int action, int modifiers) override;

    bool scrollEvent(const ng::Vector2i& p, const ng::Vector2f& rel) override;
    bool mouseMotionEvent(const ng::Vector2i& p, const ng::Vector2i& rel, int button, int modifiers) override;
    bool mouseButtonEvent(const ng::Vector2i& p, int button, bool down, int modifiers) override;
  
  private:
    void recenterCamera();
    void computeCameraMatrices(mx::Matrix44& world,
                               mx::Matrix44& view,
                               mx::Matrix44& proj);

    bool setElementSelection(int index);
    void updateElementSelections();
    void updatePropertySheet();

  private:
    ng::Window* _window;
    std::unique_ptr<Mesh> _mesh;
    MaterialPtr _material;

    CameraParameters _cameraParams;
    bool _translationActive;
    ng::Vector2i _translationStart;

    mx::FilePath _startPath;
    mx::FilePath _searchPath;
    mx::FilePath _materialFilename;
    int _envSamples;

    mx::DocumentPtr _stdLib;
    mx::DocumentPtr _materialDocument;

    std::vector<mx::ElementPtr> _elementSelections;
    int _elementSelectionIndex;
    ng::ComboBox* _elementSelectionBox;

    ng::FormHelper* _propertySheet;
    bool _showPropertySheet;
    ng::Window* _propertySheetWindow;
    bool _showNonEditableInputs;

    mx::ImageHandlerPtr _imageHandler;
};

#endif // MATERIALXVIEW_VIEWER_H
