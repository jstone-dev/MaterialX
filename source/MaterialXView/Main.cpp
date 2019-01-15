#include <MaterialXView/Viewer.h>

#include <iostream>

NANOGUI_FORCE_DISCRETE_GPU();

int main(int argc, char* const argv[])
{  
    std::vector<std::string> tokens;
    for (int i = 1; i < argc; i++)
    {
        tokens.push_back(std::string(argv[i]));
    }

    mx::StringVec libraryFolders = { "stdlib", "pbrlib", "stdlib/genglsl", "pbrlib/genglsl" };
    mx::FileSearchPath searchPath;
    mx::StringMap nodeRemap;
    for (size_t i = 0; i < tokens.size(); i++)
    {
        const std::string& token = tokens[i];
        const std::string& nextToken = i + 1 < tokens.size() ? tokens[i + 1] : mx::EMPTY_STRING;
        if (token == "--library" && !nextToken.empty())
        {
            libraryFolders.push_back(nextToken);
        }
        if (token == "--path" && !nextToken.empty())
        {
            searchPath = mx::FileSearchPath(nextToken);
        }
        if (token == "--remap" && !nextToken.empty())
        {
            mx::StringVec vec = mx::splitString(nextToken, ":");
            if (vec.size() == 2)
            {
                nodeRemap[vec[0]] = vec[1];
            }
        }
    }
    searchPath.append(mx::FilePath::getCurrentPath() / mx::FilePath("documents/Libraries"));

    try
    {
        ng::init();
        {
            ng::ref<Viewer> viewer = new Viewer(libraryFolders, searchPath, nodeRemap);
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
