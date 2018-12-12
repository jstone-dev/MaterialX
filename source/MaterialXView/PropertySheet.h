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
    void addValueToForm(mx::ValuePtr value, const std::string& label,
        const std::string& path, mx::ValuePtr min, mx::ValuePtr max,
        const mx::StringVec& enumeration, const std::vector<mx::ValuePtr> enumValues,
        const std::string& group, ng::FormHelper& form, Viewer* viewer);

      
    bool _visible;
    ng::FormHelper* _form;
    ng::Window* _formWindow;
};

// Logical item group
struct PropertySheetItem
{
    std::string label;
    mx::Shader::Variable* variable = nullptr;
    mx::UIProperties ui;
};

// Groupings of property sheet items based on a string identifier
using PropertySheetGroups = std::multimap <std::string, PropertySheetItem>;


#endif // MATERIALXVIEW_VIEWER_H
