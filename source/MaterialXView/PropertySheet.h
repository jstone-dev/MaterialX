#ifndef MATERIALXVIEW_PROPERTYSHEET_H
#define MATERIALXVIEW_PROPERTYSHEET_H

#include <MaterialXGenShader/HwShader.h>
#include <MaterialXGenShader/Util.h>
#include <MaterialXView/Material.h>

#include <nanogui/screen.h>
#include <nanogui/formhelper.h>

namespace mx = MaterialX;
namespace ng = nanogui;

class Viewer;

// Property sheet item information
struct PropertySheetItem
{
    std::string label;
    mx::Shader::Variable* variable = nullptr;
    mx::UIProperties ui;
};

// Property sheet
class PropertySheet 
{
  public:
    PropertySheet();
    void updateContents(Viewer* viewer);

    bool visible() const
    {
        return _visible;
    }

    void setVisible(bool value)
    {
        if (value != _visible)
        {
            _visible = value;
            _formWindow->setVisible(_visible);
        }
    }

  protected:
    void create(Viewer& parent);
    void addItemToForm(const PropertySheetItem& pitem, const std::string& group,
                        ng::FormHelper& form, Viewer* viewer);
      
    bool _visible;
    ng::FormHelper* _form;
    ng::Window* _formWindow;
    bool _fileDialogsForImages;
};


// Property sheet items grouped based on a string identifier
using PropertySheetGroups = std::multimap <std::string, PropertySheetItem>;


#endif // MATERIALXVIEW_VIEWER_H
