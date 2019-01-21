#include <MaterialXView/Viewer.h>

#include <MaterialXRender/Handlers/stbImageLoader.h>
#include <MaterialXRender/Handlers/TinyObjLoader.h>
#include <MaterialXGenShader/Util.h>

#include <nanogui/button.h>
#include <nanogui/combobox.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/messagedialog.h>

#include <iostream>
#include <fstream>

const float PI = std::acos(-1.0f);

const int MIN_ENV_SAMPLES = 4;
const int MAX_ENV_SAMPLES = 1024;
const int DEFAULT_ENV_SAMPLES = 16;

namespace {

mx::Matrix44 createViewMatrix(const mx::Vector3& eye,
                              const mx::Vector3& target,
                              const mx::Vector3& up)
{
    mx::Vector3 z = (target - eye).getNormalized();
    mx::Vector3 x = z.cross(up).getNormalized();
    mx::Vector3 y = x.cross(z);

    return mx::Matrix44(
         x[0],  x[1],  x[2], -x.dot(eye),
         y[0],  y[1],  y[2], -y.dot(eye),
        -z[0], -z[1], -z[2],  z.dot(eye),
         0.0f,  0.0f,  0.0f,  1.0f);
}

mx::Matrix44 createPerspectiveMatrix(float left, float right,
                                     float bottom, float top,
                                     float near, float far)
{
    return mx::Matrix44(
        (2.0f * near) / (right - left), 0.0f, (right + left) / (right - left), 0.0f,
        0.0f, (2.0f * near) / (top - bottom), (top + bottom) / (top - bottom), 0.0f,
        0.0f, 0.0f, -(far + near) / (far - near), -(2.0f * far * near) / (far - near),
        0.0f, 0.0f, -1.0f, 0.0f);
}

void writeTextFile(const std::string& text, const std::string& filePath)
{
    std::ofstream file;
    file.open(filePath);
    file << text;
    file.close();
}

} // anonymous namespace

//
// Viewer methods
//

Viewer::Viewer(const mx::StringVec& libraryFolders,
               const mx::FileSearchPath& searchPath,
               const mx::StringMap& nodeRemap) :
    ng::Screen(ng::Vector2i(1280, 960), "MaterialXView"),
    _eye(0.0f, 0.0f, 5.0f),
    _up(0.0f, 1.0f, 0.0f),
    _zoom(1.0f),
    _viewAngle(45.0f),
    _nearDist(0.05f),
    _farDist(100.0f),
    _modelZoom(1.0f),
    _translationActive(false),
    _translationStart(0, 0),
    _libraryFolders(libraryFolders),
    _searchPath(searchPath),
    _nodeRemap(nodeRemap),
    _envSamples(DEFAULT_ENV_SAMPLES),
    _geomIndex(0),
    _elemIndex(0)
{
    _window = new ng::Window(this, "Viewer Options");
    _window->setPosition(ng::Vector2i(15, 15));
    _window->setLayout(new ng::GroupLayout());

    ng::Button* meshButton = new ng::Button(_window, "Load Mesh");
    meshButton->setCallback([this]()
    {
        mProcessEvents = false;
        std::string filename = ng::file_dialog({{"obj", "Wavefront OBJ"}}, false);
        if (!filename.empty())
        {
            _geometryHandler.clearGeometry();
            if (_geometryHandler.loadGeometry(filename))
            {
                if (_material)
                {
                    _material->bindMesh(_geometryHandler);
                }
                updateGeometrySelections();
                initCamera();
            }
            else
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Mesh Loading Error", filename);
            }
        }
        mProcessEvents = true;
    });

    ng::Button* materialButton = new ng::Button(_window, "Load Material");
    materialButton->setCallback([this]()
    {
        mProcessEvents = false;
        std::string filename = ng::file_dialog({{"mtlx", "MaterialX"}}, false);
        if (!filename.empty())
        {
            _materialFilename = filename;
            try
            {
                _contentDocument = loadDocument(_materialFilename, _stdLib);
                remapNodes(_contentDocument, _nodeRemap);
                updateElementSelections();
                setElementSelection(0);
                updatePropertyEditor();
            }
            catch (std::exception& e)
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
            }
        }
        mProcessEvents = true;
    });

    ng::Button* editorButton = new ng::Button(_window, "Property Editor");
    editorButton->setFlags(ng::Button::ToggleButton);
    editorButton->setChangeCallback([this](bool state)
    { 
        _propertyEditor.setVisible(state);
        performLayout();
    });

    mx::StringVec sampleOptions;
    for (int i = MIN_ENV_SAMPLES; i <= MAX_ENV_SAMPLES; i *= 4)
    {
        mProcessEvents = false;
        sampleOptions.push_back("Samples " + std::to_string(i));
        mProcessEvents = true;
    }
    ng::ComboBox* sampleBox = new ng::ComboBox(_window, sampleOptions);
    sampleBox->setChevronIcon(-1);
    sampleBox->setSelectedIndex((int) std::log2(DEFAULT_ENV_SAMPLES / MIN_ENV_SAMPLES) / 2);
    sampleBox->setCallback([this](int index)
    {
        _envSamples = MIN_ENV_SAMPLES * (int) std::pow(4, index);
    });

    _geomLabel = new ng::Label(_window, "Geometry");

    _geomSelectionBox = new ng::ComboBox(_window, {"None"});
    _geomSelectionBox->setChevronIcon(-1);
    _geomSelectionBox->setCallback([this](int choice)
    {
        setGeometrySelection(choice);
    });

    _elemLabel = new ng::Label(_window, "Element");

    _elemSelectionBox = new ng::ComboBox(_window, {"None"});
    _elemSelectionBox->setChevronIcon(-1);
    _elemSelectionBox->setCallback([this](int choice)
    {
        setElementSelection(choice);
        updatePropertyEditor();
    });

    mx::ImageLoaderPtr stbImageLoader = mx::stbImageLoader::create();
    _imageHandler = mx::GLTextureHandler::create(stbImageLoader);
    _stdLib = loadLibraries(_libraryFolders, _searchPath);

    std::string meshFilename("documents/TestSuite/Geometry/teapot.obj");
    mx::TinyObjLoaderPtr loader = mx::TinyObjLoader::create();
    _geometryHandler.addLoader(loader);
    _geometryHandler.loadGeometry(meshFilename);
    updateGeometrySelections();
    initCamera();

    setResizeCallback([this](ng::Vector2i size)
    {
        _arcball.setSize(size);
    });

    _materialFilename = std::string("documents/TestSuite/pbrlib/materials/standard_surface_default.mtlx");

    try
    {
        _contentDocument = loadDocument(_materialFilename, _stdLib);
        remapNodes(_contentDocument, _nodeRemap);
    }
    catch (std::exception& e)
    {
        new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Failed to load document", e.what());
    }
    try
    {
        updateElementSelections();
        setElementSelection(0);
    }
    catch (std::exception& e)
    {
        new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
    }

    updatePropertyEditor();
    _propertyEditor.setVisible(false);
    performLayout();
}

void Viewer::updateGeometrySelections()
{
    _geomSelections.clear();
    for (mx::MeshPtr mesh : _geometryHandler.getMeshes())
    {
        for (size_t partIndex = 0; partIndex < mesh->getPartitionCount(); partIndex++)
        {
            mx::MeshPartitionPtr part = mesh->getPartition(partIndex);
            _geomSelections.push_back(part);
        }
    }

    std::vector<std::string> items;
    for (size_t i = 0; i < _geomSelections.size(); i++)
    {
        items.push_back(_geomSelections[i]->getIdentifier());
    }
    _geomSelectionBox->setItems(items);

    _geomLabel->setVisible(items.size() > 1);
    _geomSelectionBox->setVisible(items.size() > 1);

    performLayout();
}

bool Viewer::setGeometrySelection(size_t index)
{
    if (index < _geomSelections.size())
    {
        _geomIndex = index;
        return true;
    }
    return false;
}

void Viewer::updateElementSelections()
{
    _elemSelections.clear();
    if (_contentDocument)
    {
        mx::findRenderableElements(_contentDocument, _elemSelections);
    }

    std::vector<std::string> items;
    for (size_t i = 0; i < _elemSelections.size(); i++)
    {
        items.push_back(_elemSelections[i]->getNamePath());
    }
    _elemSelectionBox->setItems(items);

    _elemLabel->setVisible(items.size() > 1);
    _elemSelectionBox->setVisible(items.size() > 1);

    performLayout();
}

bool Viewer::setElementSelection(size_t index)
{
    mx::ElementPtr elem;
    if (index < _elemSelections.size())
    {
        elem = _elemSelections[index];
    }
    if (elem)
    {
        _material = Material::generateMaterial(_searchPath, elem);
        if (_material)
        {
            _material->bindImages(_imageHandler, _searchPath);
            _material->bindMesh(_geometryHandler);
            _elemIndex = index;
            return true;
        }
    }
    return false;
}

bool Viewer::keyboardEvent(int key, int scancode, int action, int modifiers)
{
    if (Screen::keyboardEvent(key, scancode, action, modifiers))
    {
        return true;
    }
    if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        try
        {
            _contentDocument = loadDocument(_materialFilename, _stdLib);
            remapNodes(_contentDocument, _nodeRemap);
        }
        catch (std::exception& e)
        {
            new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Failed to load document", e.what());
        }
        try
        {
            updateElementSelections();
            setElementSelection(0);
            updatePropertyEditor();
        }
        catch (std::exception& e)
        {
            new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
        }
        return true;
    }
    if (key == GLFW_KEY_S && action == GLFW_PRESS)
    {
        if (!_materialFilename.isEmpty())
        {
            try
            {
                mx::ElementPtr elem = _elemSelections.size() ? _elemSelections[0] : nullptr;
                if (elem)
                {
                    mx::HwShaderPtr hwShader = generateSource(_searchPath, elem);
                    if (hwShader)
                    {
                        std::string vertexShader = hwShader->getSourceCode(mx::HwShader::VERTEX_STAGE);
                        std::string pixelShader = hwShader->getSourceCode(mx::HwShader::PIXEL_STAGE);
                        std::string baseName = mx::splitString(_materialFilename.getBaseName(), ".")[0];
                        writeTextFile(vertexShader, _searchPath[0] / (baseName + "_vs.glsl"));
                        writeTextFile(pixelShader, _searchPath[0]  / (baseName + "_ps.glsl"));
                    }
                }
            }
            catch (std::exception& e)
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
            }
        }
        return true;
    }

    // Allow left and right keys to cycle through the renderable elements
    if ((key == GLFW_KEY_RIGHT || key == GLFW_KEY_LEFT) && action == GLFW_PRESS)
    {
        size_t elementCount = _elemSelections.size();
        if (elementCount > 1)
        {
            size_t newIndex = 0;
            if (key == GLFW_KEY_RIGHT)
            {
                newIndex = (_elemIndex < elementCount - 1) ? _elemIndex + 1 : 0;
            }
            else
            {
                newIndex = (_elemIndex > 0) ? _elemIndex - 1 : elementCount - 1;
            }
            try
            {
                if (setElementSelection(newIndex))
                {
                    _elemSelectionBox->setSelectedIndex((int) newIndex);
                    updateElementSelections();
                    updatePropertyEditor();
                }
            }
            catch (std::exception& e)
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
            }
        }
        return true;
    }

    return false;
}

void Viewer::drawContents()
{
    if (_geometryHandler.getMeshes().empty() || !_material)
    {
        return;
    }
    if (!_material->bindShader())
    {
        return;
    }

    mx::Matrix44 world, view, proj;
    computeCameraMatrices(world, view, proj);

    _material->bindViewInformation(world, view, proj);
    _material->bindImages(_imageHandler, _searchPath);
    _material->bindLights(_imageHandler, _searchPath, _envSamples);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_FRAMEBUFFER_SRGB);
    if (_material->hasTransparency())
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    else
    {
        glDisable(GL_BLEND);
    }

    _material->draw(_geometryHandler);

    glDisable(GL_BLEND);
    glDisable(GL_FRAMEBUFFER_SRGB);
}

bool Viewer::scrollEvent(const ng::Vector2i& p, const ng::Vector2f& rel)
{
    if (!Screen::scrollEvent(p, rel))
    {
        _zoom = std::max(0.1f, _zoom * ((rel.y() > 0) ? 1.1f : 0.9f));
    }
    return true;
}

bool Viewer::mouseMotionEvent(const ng::Vector2i& p,
                              const ng::Vector2i& rel,
                              int button,
                              int modifiers)
{
    if (Screen::mouseMotionEvent(p, rel, button, modifiers))
    {
        return true;
    }

    if (_arcball.motion(p))
    {
        return true;
    }

    if (_translationActive)
    {
        mx::Matrix44 world, view, proj;
        computeCameraMatrices(world, view, proj);
        mx::Matrix44 worldView = view * world;

        mx::Vector3 boxMin = _geometryHandler.getMinimumBounds();
        mx::Vector3 boxMax = _geometryHandler.getMaximumBounds();
        mx::Vector3 sphereCenter = (boxMax + boxMin) / 2.0;

        float zval = ng::project(ng::Vector3f(sphereCenter.data()),
                                 ng::Matrix4f(worldView.getTranspose().data()),
                                 ng::Matrix4f(proj.getTranspose().data()),
                                 mSize).z();
        ng::Vector3f pos1 = ng::unproject(ng::Vector3f((float) p.x(),
                                                       (float) (mSize.y() - p.y()),
                                                       (float) zval),
                                          ng::Matrix4f(worldView.getTranspose().data()),
                                          ng::Matrix4f(proj.getTranspose().data()),
                                          mSize);
        ng::Vector3f pos0 = ng::unproject(ng::Vector3f((float) _translationStart.x(),
                                                       (float) (mSize.y() - _translationStart.y()),
                                                       (float) zval),
                                          ng::Matrix4f(worldView.getTranspose().data()),
                                          ng::Matrix4f(proj.getTranspose().data()),
                                          mSize);
        ng::Vector3f delta = pos1 - pos0;
        _modelTranslation = _modelTranslationStart +
                            mx::Vector3(delta.data(), delta.data() + delta.size());

        return true;
    }

    return false;
}

bool Viewer::mouseButtonEvent(const ng::Vector2i& p, int button, bool down, int modifiers)
{
    if (!Screen::mouseButtonEvent(p, button, down, modifiers))
    {
        if (button == GLFW_MOUSE_BUTTON_1 && !modifiers)
        {
            _arcball.button(p, down);
        }
        else if (button == GLFW_MOUSE_BUTTON_2 ||
                (button == GLFW_MOUSE_BUTTON_1 && modifiers == GLFW_MOD_SHIFT))
        {
            _modelTranslationStart = _modelTranslation;
            _translationActive = true;
            _translationStart = p;
        }
    }
    if (button == GLFW_MOUSE_BUTTON_1 && !down)
    {
        _arcball.button(p, false);
    }
    if (!down)
    {
        _translationActive = false;
    }
    return true;
}

void Viewer::initCamera()
{
    _arcball = ng::Arcball();
    _arcball.setSize(mSize);

    mx::Vector3 boxMin = _geometryHandler.getMinimumBounds();
    mx::Vector3 boxMax = _geometryHandler.getMaximumBounds();
    mx::Vector3 sphereCenter = (boxMax + boxMin) / 2.0;
    float sphereRadius = (sphereCenter - boxMin).getMagnitude();
    _modelZoom = 2.0f / sphereRadius;
    _modelTranslation = sphereCenter * -1.0f;
}

void Viewer::computeCameraMatrices(mx::Matrix44& world,
                                   mx::Matrix44& view,
                                   mx::Matrix44& proj)
{
    float fH = std::tan(_viewAngle / 360.0f * PI) * _nearDist;
    float fW = fH * (float) mSize.x() / (float) mSize.y();

    ng::Matrix4f ngArcball = _arcball.matrix();
    mx::Matrix44 arcball = mx::Matrix44(ngArcball.data(), ngArcball.data() + ngArcball.size()).getTranspose();

    view = createViewMatrix(_eye, _center, _up) * arcball;
    proj = createPerspectiveMatrix(-fW, fW, -fH, fH, _nearDist, _farDist);
    world = mx::Matrix44::createScale(mx::Vector3(_zoom * _modelZoom));
    world *= mx::Matrix44::createTranslation(_modelTranslation).getTranspose();
}

void Viewer::updatePropertyEditor()
{
    _propertyEditor.updateContents(this);
}
