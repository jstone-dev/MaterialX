#include <MaterialXView/Viewer.h>

#include <nanogui/button.h>
#include <nanogui/combobox.h>
#include <nanogui/label.h>
#include <nanogui/layout.h>
#include <nanogui/messagedialog.h>

#include <iostream>
#include <fstream>

#include <MaterialXRender/Handlers/stbImageLoader.h>
#include <MaterialXRender/Handlers/TinyExrImageLoader.h>

const float PI = std::acos(-1.0f);

const int MIN_ENV_SAMPLES = 16;
const int MAX_ENV_SAMPLES = 256;

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
    _libraryFolders(libraryFolders),
    _searchPath(searchPath),
    _nodeRemap(nodeRemap),
    _translationActive(false),
    _translationStart(0, 0),
    _envSamples(MIN_ENV_SAMPLES)
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
            _mesh = MeshPtr(new Mesh());
            if (_mesh->loadMesh(filename))
            {
                if (_material)
                {
                    _material->bindMesh(_mesh);
                }
                initCamera();
            }
            else
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Mesh Loading Error", filename);
                _mesh = nullptr;
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
                loadDocument(_materialFilename, _materialDocument, _stdLib, _elementSelections);
                remapNodes(_materialDocument, _nodeRemap);
                updateElementSelections();
                setElementSelection(0);
                updatePropertySheet();
            }
            catch (std::exception& e)
            {
                new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Shader Generation Error", e.what());
            }
        }
        mProcessEvents = true;
    });

    _elementSelectionBox = new ng::ComboBox(_window, {"None"});
    _elementSelectionBox->setChevronIcon(-1);
    _elementSelectionBox->setCallback([this](int choice)
    {
        setElementSelection(choice);
        updatePropertySheet();
    });

    ng::Button* propertySheetButton = new ng::Button(_window, "Property Sheet");
    propertySheetButton->setFlags(ng::Button::ToggleButton);
    propertySheetButton->setChangeCallback([this](bool state)
    { 
        _showPropertySheet = state;
        this->_propertySheetWindow->setVisible(_showPropertySheet);
        performLayout();
    });

    mx::StringVec sampleOptions;
    for (int i = MIN_ENV_SAMPLES; i <= MAX_ENV_SAMPLES; i *= 2)
    {
        mProcessEvents = false;
        sampleOptions.push_back("Samples " + std::to_string(i));
        mProcessEvents = true;
    }
    ng::ComboBox* sampleBox = new ng::ComboBox(_window, sampleOptions);
    sampleBox->setChevronIcon(-1);
    sampleBox->setCallback([this](int index)
    {
        _envSamples = MIN_ENV_SAMPLES * (int) std::pow(2, index);
    });

    _stdLib = mx::createDocument();
    _materialFilename = std::string("documents/TestSuite/sxpbrlib/materials/standard_surface_default.mtlx");

    mx::ImageLoaderPtr exrImageLoader = mx::TinyEXRImageLoader::create();
    mx::ImageLoaderPtr stbImageLoader = mx::stbImageLoader::create();
    _imageHandler = mx::GLTextureHandler::create(exrImageLoader);
    _imageHandler->addLoader(stbImageLoader);
    loadLibraries(_libraryFolders, _searchPath, _stdLib);

    _mesh = MeshPtr(new Mesh());
    _mesh->loadMesh("documents/TestSuite/Geometry/teapot.obj");
    initCamera();

    setResizeCallback([this](ng::Vector2i size)
    {
        _cameraParams.arcball.setSize(size);
    });

    try
    {
        loadDocument(_materialFilename, _materialDocument, _stdLib, _elementSelections);
        remapNodes(_materialDocument, _nodeRemap);
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

    // By default hind inputs which cannot be edited. e.g. for a shader ref there may be a lot of inputs
    // which have not been overridden and thus are just the defaults.
    _showNonEditableInputs = false;
    _propertySheet = nullptr;
    _propertySheetWindow = nullptr;
    _showPropertySheet = false; // Start up with property sheet hidden
    updatePropertySheet();

    performLayout();
}

void Viewer::updateElementSelections()
{
    std::vector<std::string> items;
    for (size_t i = 0; i < _elementSelections.size(); i++)
    {
        items.push_back(_elementSelections[i]->getNamePath());
    }
    _elementSelectionBox->setItems(items);
    _elementSelectionBox->setVisible(items.size() > 1);
    performLayout();
}

bool Viewer::setElementSelection(int index)
{
    mx::ElementPtr elem;
    if (index >= 0 && index < _elementSelections.size())
    {
        elem = _elementSelections[index];
    }
    if (elem)
    {
        _material = Material::generateShader(_searchPath, elem);
        if (_material)
        {
            _material->bindImages(_imageHandler, _searchPath);
            _material->bindMesh(_mesh);
            _elementSelectionIndex = index;
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
            loadDocument(_materialFilename, _materialDocument, _stdLib, _elementSelections);
            remapNodes(_materialDocument, _nodeRemap);
        }
        catch (std::exception& e)
        {
            new ng::MessageDialog(this, ng::MessageDialog::Type::Warning, "Failed to load document", e.what());
        }
        try
        {
            updateElementSelections();
            setElementSelection(0);
            updatePropertySheet();
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
                mx::ElementPtr elem = _elementSelections.size() ? _elementSelections[0] : nullptr;
                if (elem)
                {
                    mx::HwShaderPtr hwShader = nullptr;
                    StringPair source = generateSource(_searchPath, hwShader, elem);
                    std::string baseName = mx::splitString(_materialFilename.getBaseName(), ".")[0];
                    mx::StringVec splitName = mx::splitString(baseName, ".");
                    writeTextFile(source.first, _searchPath[0] / (baseName + "_vs.glsl"));
                    writeTextFile(source.second, _searchPath[0]  / (baseName + "_ps.glsl"));
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
        int elementSize = static_cast<int>(_elementSelections.size());
        if (elementSize > 1)
        {
            int newIndex = 0;
            if (key == GLFW_KEY_RIGHT)
            {
                newIndex = (_elementSelectionIndex + 1) % elementSize;
            }
            else
            {
                newIndex = (_elementSelectionIndex + elementSize - 1) % elementSize;
            }
            try
            {
                if (setElementSelection(newIndex))
                {
                    _elementSelectionBox->setSelectedIndex(newIndex);
                    updateElementSelections();
                    updatePropertySheet();
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
    if (!_mesh || !_material)
    {
        return;
    }

    mx::Matrix44 world, view, proj;
    computeCameraMatrices(world, view, proj);

    GLShaderPtr shader = _material->ngShader();
    shader->bind();

    _material->bindViewInformation(world, view, proj);
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

    shader->drawIndexed(GL_TRIANGLES, 0, (uint32_t) _mesh->getFaceCount());

    glDisable(GL_BLEND);
    glDisable(GL_FRAMEBUFFER_SRGB);
}

bool Viewer::scrollEvent(const ng::Vector2i& p, const ng::Vector2f& rel)
{
    if (!Screen::scrollEvent(p, rel))
    {
        _cameraParams.zoom = std::max(0.1f, _cameraParams.zoom * ((rel.y() > 0) ? 1.1f : 0.9f));
    }
    return true;
}

bool Viewer::mouseMotionEvent(const ng::Vector2i& p,
                              const ng::Vector2i& rel,
                              int button,
                              int modifiers)
{
    if (!Screen::mouseMotionEvent(p, rel, button, modifiers))
    {
        if (_cameraParams.arcball.motion(p))
        {
        }
        else if (_translationActive)
        {
            mx::Matrix44 world, view, proj;
            computeCameraMatrices(world, view, proj);
            mx::Matrix44 worldView = view * world;
            float zval = ng::project(ng::Vector3f(_mesh->getSphereCenter().data()),
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
            _cameraParams.modelTranslation = _cameraParams.modelTranslationStart +
                                             mx::Vector3(delta.data(), delta.data() + delta.size());
        }
    }
    return true;
}

bool Viewer::mouseButtonEvent(const ng::Vector2i& p, int button, bool down, int modifiers)
{
    if (!Screen::mouseButtonEvent(p, button, down, modifiers))
    {
        if (button == GLFW_MOUSE_BUTTON_1 && !modifiers)
        {
            _cameraParams.arcball.button(p, down);
        }
        else if (button == GLFW_MOUSE_BUTTON_2 ||
                (button == GLFW_MOUSE_BUTTON_1 && modifiers == GLFW_MOD_SHIFT))
        {
            _cameraParams.modelTranslationStart = _cameraParams.modelTranslation;
            _translationActive = true;
            _translationStart = p;
        }
    }
    if (button == GLFW_MOUSE_BUTTON_1 && !down)
    {
        _cameraParams.arcball.button(p, false);
    }
    if (!down)
    {
        _translationActive = false;
    }
    return true;
}

void Viewer::initCamera()
{
    _cameraParams.arcball = ng::Arcball();
    _cameraParams.arcball.setSize(mSize);
    _cameraParams.modelZoom = 2.0f / _mesh->getSphereRadius();
    _cameraParams.modelTranslation = _mesh->getSphereCenter() * -1.0f;
}

void Viewer::computeCameraMatrices(mx::Matrix44& world,
                                   mx::Matrix44& view,
                                   mx::Matrix44& proj)
{
    float fH = std::tan(_cameraParams.viewAngle / 360.0f * PI) * _cameraParams.dnear;
    float fW = fH * (float) mSize.x() / (float) mSize.y();

    ng::Matrix4f ngArcball = _cameraParams.arcball.matrix();
    mx::Matrix44 arcball = mx::Matrix44(ngArcball.data(), ngArcball.data() + ngArcball.size()).getTranspose();

    view = createViewMatrix(_cameraParams.eye, _cameraParams.center, _cameraParams.up) * arcball;
    proj = createPerspectiveMatrix(-fW, fW, -fH, fH, _cameraParams.dnear, _cameraParams.dfar);
    world = mx::Matrix44::createScale(mx::Vector3(_cameraParams.zoom * _cameraParams.modelZoom));
    world *= mx::Matrix44::createTranslation(_cameraParams.modelTranslation).getTranspose();
}

void Viewer::addValueToForm(mx::ValuePtr value, const std::string& label,
    const std::string& path, ng::FormHelper& form)
{
    if (!value)
    {
        return;
    }
    if (value->isA<int>())
    {
        int v = value->asA<int>();
        nanogui::detail::FormWidget<int, std::true_type>* intVar =
            form.addVariable(label, v, true);
        intVar->setSpinnable(true);
        intVar->setCallback([this, path](int v)
        {
            if (_material)
            {
                mx::Shader::Variable* uniform = _material ? _material->findUniform(path) : nullptr;
                if (uniform)
                {
                    _material->ngShader()->bind();
                    _material->ngShader()->setUniform(uniform->name, v);
                }
            }
        });
    }
    else if (value->isA<float>())
    {
        float v = value->asA<float>();
        nanogui::detail::FormWidget<float, std::true_type>* floatVar =
            form.addVariable(label, v, true);
        floatVar->setSpinnable(true);
        floatVar->setCallback([this, path](float v)
        {
            if (_material)
            {
                mx::Shader::Variable* uniform = _material ? _material->findUniform(path) : nullptr;
                if (uniform)
                {
                    _material->ngShader()->bind();
                    _material->ngShader()->setUniform(uniform->name, v);
                }                
            }
        });
    }
    else if (value->isA<mx::Color2>())
    {
        mx::Color2 v = value->asA<mx::Color2>();
        ng::Color c;
        c.r() = v[0];
        c.g() = 0.0f;
        c.b() = 0.0f;
        c.w() = v[1];
        nanogui::detail::FormWidget<nanogui::Color, std::true_type>* colorVar =
            form.addVariable(label, c, false);
        colorVar->setFinalCallback([this, path](const ng::Color &c)
        {
            mx::Shader::Variable* uniform = _material ? _material->findUniform(path) : nullptr;
            if (uniform)
            {
                _material->ngShader()->bind();
                ng::Vector2f v;
                v.x() = c.r();
                v.y() = c.g();
                _material->ngShader()->setUniform(uniform->name, v);
            }
        });
    }
    else if (value->isA<mx::Color3>())
    {
        mx::Color3 v = value->asA<mx::Color3>();
        ng::Color c;
        c.r() = v[0];
        c.g() = v[1];
        c.b() = v[2];
        c.w() = 1.0;
        nanogui::detail::FormWidget<nanogui::Color, std::true_type>* colorVar =
            form.addVariable(label, c, true);
        colorVar->setFinalCallback([this, path](const ng::Color &c)
        {
            mx::Shader::Variable* uniform = _material ? _material->findUniform(path) : nullptr;
            if (uniform)
            { 
                _material->ngShader()->bind();
                ng::Vector3f v;
                v.x() = c.r();
                v.y() = c.g();
                v.z() = c.b();
                _material->ngShader()->setUniform(uniform->name, v);
            }
        });
    }
    else if (value->isA<mx::Color4>())
    {
        mx::Color4 v = value->asA<mx::Color4>();
        ng::Color c;
        c.r() = v[0];
        c.g() = v[1];
        c.b() = v[2];
        c.w() = v[3];
        nanogui::detail::FormWidget<nanogui::Color, std::true_type>* colorVar =
            form.addVariable(label, c, true);
        colorVar->setFinalCallback([this, path](const ng::Color &c)
        {
            mx::Shader::Variable* uniform = _material ? _material->findUniform(path) : nullptr;
            if (uniform)
            {
                _material->ngShader()->bind();
                ng::Vector4f v;
                v.x() = c.r();
                v.y() = c.g();
                v.z() = c.b();
                v.w() = c.w();
                _material->ngShader()->setUniform(uniform->name, v);
            }
        });
    }
    else if (value->isA<mx::Vector2>())
    {
        mx::Vector2 v = value->asA<mx::Vector2>();
        ng::Color c;
        c.r() = v[0];
        c.g() = v[1];
        c.b() = 0.0f;
        c.w() = 1.0f;
        nanogui::detail::FormWidget<nanogui::Color, std::true_type>* colorVar =
            form.addVariable(label, c, true);
        colorVar->setFinalCallback([this, path](const ng::Color &c)
        {
            mx::Shader::Variable* uniform = _material ? _material->findUniform(path) : nullptr;
            if (uniform)
            {
                _material->ngShader()->bind();
                ng::Vector2f v;
                v.x() = c.r();
                v.y() = c.g();
                _material->ngShader()->setUniform(uniform->name, v);
            }
        });
    }
    else if (value->isA<mx::Vector3>())
    {
        mx::Vector3 v = value->asA<mx::Vector3>();
        ng::Color c;
        c.r() = v[0];
        c.g() = v[1];
        c.b() = v[2];
        c.w() = 1.0;
        nanogui::detail::FormWidget<nanogui::Color, std::true_type>* colorVar =
            form.addVariable(label, c, true);
        colorVar->setFinalCallback([this, path](const ng::Color &c)
        {
            mx::Shader::Variable* uniform = _material ? _material->findUniform(path) : nullptr;
            if (uniform)
            {
                _material->ngShader()->bind();
                ng::Vector3f v;
                v.x() = c.r();
                v.y() = c.g();
                v.z() = c.b();
                _material->ngShader()->setUniform(uniform->name, v);
            }
        });
    }
    else if (value->isA<mx::Vector4>())
    {
        mx::Vector4 v = value->asA<mx::Vector4>();
        ng::Color c;
        c.r() = v[0];
        c.g() = v[1];
        c.b() = v[2];
        c.w() = v[3];
        nanogui::detail::FormWidget<nanogui::Color, std::true_type>* colorVar =
            form.addVariable(label, c, true);
        colorVar->setFinalCallback([this, path](const ng::Color &c)
        {
            mx::Shader::Variable* uniform = _material ? _material->findUniform(path) : nullptr;
            if (uniform)
            {
                _material->ngShader()->bind();
                ng::Vector4f v;
                v.x() = c.r();
                v.y() = c.g();
                v.z() = c.b();
                v.w() = c.w();
                _material->ngShader()->setUniform(uniform->name, v);
            }
        });
    }
    else if (value->isA<std::string>())
    {
        std::string v = value->asA<std::string>();
        if (!v.empty())
        {
            nanogui::detail::FormWidget<std::string, std::true_type>* stringVar =
                form.addVariable(label, v, true);
            stringVar->setCallback([this, path](const std::string &v)
            {
                mx::Shader::Variable* uniform = _material ? _material->findUniform(path) : nullptr;
                if (uniform)
                {
                    if (uniform->type == MaterialX::Type::FILENAME)
                    {
                        const std::string& uniformName = uniform->name;
                        const std::string& filename = _searchPath.find(v);
                        mx::ImageDesc desc;
                        uniform->value = mx::Value::createValue<std::string>(filename);
                        _material->ngShader()->bind();
                        _material->bindImage(filename, uniformName, _imageHandler, desc);
                    }
                    else
                    {
                        // TO-DO: Need remapping information from string to integer which is currently
                        // not available. 
                    }
                }   
            });
        }
    }
}

void Viewer::updatePropertySheet()
{
    if (!_propertySheet)
    {
        _propertySheet = new ng::FormHelper(this);
    }
   
    // Remove the window associated with the form.
    // This is done by explicitly creating and owning the window
    // as opposed to having it being done by the form
    ng::Vector2i previousPosition(15, _window->height() + 60);
    if (_propertySheetWindow)
    {
        for (int i = 0; i < _propertySheetWindow->childCount(); i++)
        {
            _propertySheetWindow->removeChild(i);
        }
        // We don't want the property sheet to move when
        // we update it's contents so cache any previous position
        // to use when we create a new window.
        previousPosition = _propertySheetWindow->position();
        this->removeChild(_propertySheetWindow);
    }
    _propertySheetWindow = new ng::Window(this, "Property Sheet");
    ng::AdvancedGridLayout* layout = new ng::AdvancedGridLayout({ 10, 0, 10, 0 }, {});
    layout->setMargin(10);
    layout->setColStretch(2, 1);
    _propertySheetWindow->setPosition(previousPosition);
    _propertySheetWindow->setVisible(_showPropertySheet);
    _propertySheetWindow->setLayout(layout);
    _propertySheet->setWindow(_propertySheetWindow);

    mx::ElementPtr element = nullptr;
    if (_elementSelectionIndex >= 0 && _elementSelectionIndex < _elementSelections.size())
    {
        element = _elementSelections[_elementSelectionIndex];
    }
    if (!element)
    {
        return;
    }

    if (_material && _materialDocument)
    {
        GLShaderPtr shader = _material->ngShader();
        mx::HwShaderPtr hwShader = _material->mxShader();
        if (hwShader && shader)
        {
            const MaterialX::Shader::VariableBlock publicUniforms = hwShader->getUniformBlock(MaterialX::Shader::PIXEL_STAGE, MaterialX::Shader::PUBLIC_UNIFORMS);
            for (auto uniform : publicUniforms.variableOrder)
            {
                if (uniform->path.size() && uniform->value)
                {
                    mx::ElementPtr uniformElement = _materialDocument->getDescendant(uniform->path);
                    if (uniformElement && uniformElement->isA<mx::ValueElement>())
                    {
                        addValueToForm(uniform->value, uniformElement->getName(), uniform->path, *_propertySheet);
                    }
                }
            }
        }
    }
    performLayout();
}
