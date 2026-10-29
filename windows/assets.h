#pragma once
#include "core.h"
#include <windows.h>
#include "embedded_table.h"

// Default resources are read directly from the module. Nothing is unpacked to
// disk, and no network or writable executable directory is required to run.
class Assets {
    fs::path folder;
public:
    explicit Assets(fs::path path = {}) : folder(std::move(path)) {}
    static std::string embedded(const std::string& name, size_t limit=16*1024*1024) {
        for(auto& entry : embeddedEntries) if(name == entry.name) {
            HMODULE module=GetModuleHandle(nullptr);
            HRSRC resource=FindResource(module,MAKEINTRESOURCE(entry.id),RT_RCDATA);
            if(!resource) throw std::runtime_error("Missing embedded resource: "+name);
            DWORD size=SizeofResource(module,resource);
            HGLOBAL data=LoadResource(module,resource);
            const void* bytes=data?LockResource(data):nullptr;
            if(!bytes || !size || size>limit) throw std::runtime_error("Invalid embedded resource: "+name);
            return std::string(static_cast<const char*>(bytes),size);
        }
        throw std::runtime_error("Unknown embedded resource: "+name);
    }
    std::string read(const std::string& name,size_t limit=16*1024*1024) const {
        return folder.empty()?embedded(name,limit):readText(safeFile(folder,name),limit);
    }
};
