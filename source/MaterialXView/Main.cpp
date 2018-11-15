#include <MaterialXView/Viewer.h>

#include <iostream>

int main()
{  
    try
    {
        ng::init();
        {
            ng::ref<Viewer> viewer = new Viewer();
            viewer->setVisible(true);
            ng::mainloop(-1);
        }
    
        ng::shutdown();
    }
    catch (const std::runtime_error& e)
    {
        std::string error_msg = std::string("Fatal error: ") + std::string(e.what());
        std::cerr << error_msg << std::endl;
        return -1;
    }

    return 0;
}
